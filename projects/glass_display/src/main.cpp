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
#include "cyd_display.h"
#include "glass_screen_controller.h"

static CYDDisplay              backlight(GPIO_BACKLIGHT_PIN);
static ESPNowReceiver          receiver;
static ServerConnectionMonitor connection_monitor;
static GlassScreenController   screen;

void setup() {
    Serial.begin(115200);
    delay(1500);  // let the USB-CDC monitor attach before the first prints
    Serial.println("=== glass_display SETUP START ===");

    // Backlight first, so a later failure shows a lit (if blank) panel instead
    // of a dead-looking board.
    backlight.begin();
    backlight.setBacklightPercent(100);

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
