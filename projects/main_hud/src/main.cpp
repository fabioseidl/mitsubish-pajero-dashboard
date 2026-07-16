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
#include "hud_brightness.h"
#include "hud_screen_controller.h"

static HudDisplay              display(GPIO_BACKLIGHT_PIN);
static HudBrightness           brightness(display);
static ESPNowReceiver          receiver;
static ServerConnectionMonitor connection_monitor;
static HudScreenController     screen(brightness);

void setup() {
    Serial.begin(115200);
    delay(1500);  // let the serial monitor attach before the first prints
    Serial.println("=== main_hud SETUP START ===");

    // Order matters: Arduino_GFX reconfigures GPIOs during init, so the LEDC
    // backlight is attached only after the panel is up.
    if (!screen.begin()) {
        Serial.println("[MAIN] screen init failed — halting");
        return;
    }
    display.begin();
    brightness.applyCurrent();

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
