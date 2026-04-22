#ifndef UNIT_TEST
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>

#include "cyd_display.h"
#include "brightness_controller.h"
#include "espnow_receiver.h"
#include "server_connection_monitor.h"
#include "cyd_screen_controller.h"
#include "security_config.h"
#include "pin_config.h"

static CYDDisplay              display(GPIO_BACKLIGHT_PIN);
static BrightnessController    brightness(display);
static ESPNowReceiver          receiver;
static ServerConnectionMonitor connection_monitor;
static CYDScreenController     screen(brightness);

void setup() {
    display.begin();
    brightness.applyInitial();
    screen.begin();

    connection_monitor.setStatusChangeCallback([](bool online) {
        screen.onServerStatusChanged(online);
    });

    receiver.setCallback([](const Payload& p) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        connection_monitor.onPayloadReceived(now_ms);
        screen.onPayloadReceived(p);
    });
    receiver.begin(PMK_KEY);
}

void loop() {
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    connection_monitor.tick(now_ms);
    screen.tick();
    vTaskDelay(pdMS_TO_TICKS(5));
}
#endif
