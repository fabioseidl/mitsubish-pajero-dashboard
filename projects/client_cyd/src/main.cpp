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

static bool     s_receiver_ok   = false;
static uint32_t s_rx_count      = 0;
static int      s_wifi_channel  = 0;

void setup() {
    Serial.begin(115200);

    display.begin();
    brightness.applyInitial();
    screen.begin();

    connection_monitor.setStatusChangeCallback([](bool online) {
        screen.onServerStatusChanged(online);
    });

    receiver.setCallback([](const Payload& p) {
        ++s_rx_count;
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        connection_monitor.onPayloadReceived(now_ms);
        screen.onPayloadReceived(p);
    });
    s_receiver_ok  = receiver.begin(PMK_KEY);
    s_wifi_channel = WiFi.channel();
}

void loop() {
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    connection_monitor.tick(now_ms);
    screen.tick();

    static uint32_t s_last_log_ms = 0;
    if (now_ms - s_last_log_ms >= 5000) {
        s_last_log_ms = now_ms;
        Serial.printf("[LOOP] ok=%d ch=%d raw=%lu rx=%lu up=%lus\n",
                      s_receiver_ok, s_wifi_channel,
                      (unsigned long)ESPNowReceiver::raw_rx_count_,
                      (unsigned long)s_rx_count,
                      (unsigned long)(now_ms / 1000));
    }

    vTaskDelay(pdMS_TO_TICKS(5));
}
#endif
