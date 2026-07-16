#ifndef UNIT_TEST
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <WiFi.h>

#include "espnow_receiver.h"
#include "server_connection_monitor.h"
#include "security_config.h"
#include "pin_config.h"
#include "hud_display.h"
#include "step_brightness.h"
#include "hud_screen_controller.h"

static HudDisplay              display(GPIO_BACKLIGHT_PIN);
static StepBrightness           brightness(display);
static ESPNowReceiver          receiver;
static ServerConnectionMonitor connection_monitor;
static HudScreenController     screen(brightness);

void setup() {
    Serial.begin(115200);
    delay(1500);  // let the serial monitor attach before the first prints
    Serial.println("=== main_hud SETUP START ===");

    // Backlight first, and unconditionally. GPIO_BACKLIGHT_PIN is not part of
    // the QSPI bus, so Arduino_GFX's init cannot disturb it — and lighting it
    // up front means a later failure shows a lit (if blank) panel instead of a
    // dead-looking board, which is the difference between a diagnosable fault
    // and a mystery.
    display.begin();
    brightness.applyCurrent();

    // PSRAM carries the 307 KB canvas framebuffer; without it the panel cannot
    // come up at all, so say so plainly rather than failing later in the dark.
    Serial.printf("[MAIN] PSRAM: %s (%u bytes free)\n",
                  psramFound() ? "found" : "NOT FOUND — canvas alloc will fail",
                  (unsigned)ESP.getFreePsram());

    if (!screen.begin()) {
        Serial.println("[MAIN] screen init failed — backlight is on, panel stays blank");
        return;
    }

    connection_monitor.setStatusChangeCallback([](bool online) {
        screen.onServerStatusChanged(online);
    });

    receiver.setCallback([](const Payload& p) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        connection_monitor.onPayloadReceived(now_ms);
        screen.onPayloadReceived(p);
    });

    if (!receiver.begin(PMK_KEY)) {
        Serial.println("[MAIN] ESP-NOW init failed");
    }
    Serial.printf("[MAIN] listening on WiFi channel %d\n", WiFi.channel());
}

void loop() {
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    connection_monitor.tick(now_ms);  // evaluate timeout → fires onServerStatusChanged
    screen.tick();                    // keeps LVGL rendering — must not be skipped
    vTaskDelay(pdMS_TO_TICKS(5));
}
#endif
