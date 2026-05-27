#include <Arduino.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG_MAIN = "main";

#include "espnow_receiver.h"
#include "server_connection_monitor.h"
#include "debug_screen.h"
#include "security_config.h"

// ---------------------------------------------------------------------------
// Static objects — constructed before setup(), order matches client_simple_hud
// ---------------------------------------------------------------------------
static ESPNowReceiver          s_receiver;
static ServerConnectionMonitor s_monitor;
static DebugScreen             s_screen;

// ---------------------------------------------------------------------------
// setup() — hardware init + ESP-NOW wiring
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(1500);  // Allow USB-CDC to enumerate and serial monitor to attach

    ESP_LOGI(TAG_MAIN, "booting main_display_debug");

    // Initialise display hardware, LVGL, and build the PID table UI.
    // All peripheral resets (LCD, touch) go through the I2C expander inside begin().
    if (!s_screen.begin()) {
        ESP_LOGE(TAG_MAIN, "FATAL: DebugScreen::begin() failed — halting");
        while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    // Wire server connection monitor → screen status callback
    s_monitor.setStatusChangeCallback([](bool online) {
        s_screen.onServerStatusChanged(online);
    });

    // Wire ESP-NOW receiver → connection monitor + screen payload callback
    s_receiver.setCallback([](const Payload& p) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        s_monitor.onPayloadReceived(now_ms);
        s_screen.onPayloadReceived(p);
    });

    // Start ESP-NOW with the shared PMK key (must match the server firmware)
    s_receiver.begin(PMK_KEY);

    ESP_LOGI(TAG_MAIN, "setup complete — entering loop");
}

// ---------------------------------------------------------------------------
// loop() — 5 ms tick (same cadence as client_simple_hud)
// ---------------------------------------------------------------------------
void loop() {
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

    // Check server timeout; fires onServerStatusChanged() callback if state changes
    s_monitor.tick(now_ms);

    // Drain pending payloads/status, update LVGL widgets, drive rendering pipeline
    s_screen.tick();

    vTaskDelay(pdMS_TO_TICKS(5));
}
