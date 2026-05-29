#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>

#include "lgfx_config.h"
#include "main_display.h"
#include "screen_controller.h"
#include "brightness_controller.h"
#include "espnow_receiver.h"
#include "server_connection_monitor.h"
#include "security_config.h"
#include "payload.h"

// ── Singletons ────────────────────────────────────────────────────────────────

static LGFX                   g_lcd;
static MainDisplay             g_display(g_lcd);
static BrightnessController    g_brightness(g_display);
static MainScreenController    g_screen(g_lcd, g_brightness);
static ServerConnectionMonitor g_monitor;
static ESPNowReceiver          g_receiver;

// ── setup ─────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(1000);  // match _display_test.cpp — lets power rails settle
    Serial.println("[main_display] boot");

    // 1. PCF8574: LCD reset, touch reset, backlight — must happen before lcd.init()
    g_display.begin();

    // 2. LGFX init + LVGL + SquareLine UI
    g_screen.begin();

    // 2b. Re-assert PCF8574 backlight after lcd.init() — Bus_RGB init can disturb
    //     the I2C bus or GPIO state, causing PCF8574 to reset its outputs.
    g_display.reapplyBacklight();

    // 3. Apply initial brightness via lcd.setBrightness()
    g_brightness.applyInitial();

    // 4. Wire up server connection state changes to the display
    g_monitor.setStatusChangeCallback([](bool online) {
        g_screen.onConnectionChange(online);
    });

    // 5. Wire up ESP-NOW payload to connection monitor and display
    //    Runs in the WiFi task; onPayload() is ISR-safe.
    g_receiver.setCallback([](const Payload& p) {
        uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
        g_monitor.onPayloadReceived(now_ms);
        g_screen.onPayload(p);
    });

    // 6. Start ESP-NOW receiver (PMK from security_config.h — never commit that file)
    if (!g_receiver.begin(PMK_KEY)) {
        Serial.println("[main_display] ESP-NOW init failed");
    }

    // 7. Re-assert backlight one more time after WiFi init.
    //    WiFi radio startup can cause a brief brownout that drops the PCF8574
    //    output latch — without this, the BL bit goes low and the screen stays
    //    dark even though the rest of the pipeline is healthy.
    g_display.reapplyBacklight();

    Serial.println("[main_display] ready");
}

// ── loop ──────────────────────────────────────────────────────────────────────

void loop() {
    uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);

    // Defensive: re-assert PCF8574 backlight every 200 ms. If WiFi or any
    // other peripheral is glitching the I2C rail and dropping the latch,
    // this keeps the backlight on.
    static uint32_t last_bl_ms = 0;
    if (now_ms - last_bl_ms >= 200) {
        last_bl_ms = now_ms;
        g_display.reapplyBacklight();
    }

    g_monitor.tick(now_ms);
    g_screen.tick();
    vTaskDelay(pdMS_TO_TICKS(5));
}
