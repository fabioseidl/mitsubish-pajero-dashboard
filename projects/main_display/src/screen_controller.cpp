#include "screen_controller.h"

#include <Arduino.h>
#include <string.h>
#include <esp_heap_caps.h>

#include "lvgl/lvgl.h"
#include "dashboard_ui.h"

// ─────────────────────────────────────────────────────────────
// LVGL 9.5 display flush callback
//
// LVGL 8 used lv_disp_drv_t + a lv_color_t* with a `.full` union member.
// LVGL 9 passes the rendered region as a raw uint8_t* byte buffer in the
// display's color format (RGB565 here) and the display handle directly.
// ─────────────────────────────────────────────────────────────

static LGFX*                 s_lcd        = nullptr;
static BrightnessController* s_brightness = nullptr;

static void lvgl_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    static bool first = true;
    if (first) {
        first = false;
        Serial.printf("[lvgl] first flush (%d,%d)-(%d,%d)\n",
                      (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2);
    }

    const int32_t w = area->x2 - area->x1 + 1;
    const int32_t h = area->y2 - area->y1 + 1;

    s_lcd->startWrite();
    s_lcd->pushImage(area->x1, area->y1, w, h, reinterpret_cast<uint16_t*>(px_map));
    s_lcd->endWrite();

    lv_display_flush_ready(disp);
}

// ─────────────────────────────────────────────────────────────
// LVGL 9.5 touch read callback
//
// LVGL 8 used lv_indev_drv_t; LVGL 9 passes the lv_indev_t* handle. The
// data struct fields (state, point) are unchanged.
//
// NOTE: the GT911 is intentionally not registered in lgfx_config.h (its I2C
// init conflicts with the Arduino Wire bus that drives the PCF8574). getTouch()
// therefore returns 0 today, so this device stays released — it is wired up so
// touch-to-brighten works the moment GT911 is enabled in LovyanGFX.
// ─────────────────────────────────────────────────────────────

static void lvgl_touch_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
    lgfx::touch_point_t tp;
    if (s_lcd && s_lcd->getTouch(&tp) > 0) {
        data->state   = LV_INDEV_STATE_PRESSED;
        data->point.x = static_cast<int32_t>(tp.x);
        data->point.y = static_cast<int32_t>(tp.y);
        if (s_brightness) s_brightness->onTouch(millis());
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ─────────────────────────────────────────────────────────────
// MainScreenController
// ─────────────────────────────────────────────────────────────

MainScreenController::MainScreenController(LGFX& lcd, BrightnessController& brightness)
    : lcd_(lcd),
      brightness_(brightness),
      payload_mux_(portMUX_INITIALIZER_UNLOCKED),
      new_payload_(false),
      pending_payload_(nullptr),
      status_dirty_(false),
      status_online_(false),
      last_tick_ms_(0) {}

void MainScreenController::begin() {
    s_lcd            = &lcd_;
    s_brightness     = &brightness_;
    pending_payload_ = new Payload{};

    // LovyanGFX RGB panel init (PCF8574 reset/backlight already released in main).
    if (!lcd_.init()) {
        Serial.println("[screen] lcd.init() failed");
    }
    lcd_.setBrightness(255);
    lcd_.fillScreen(TFT_BLACK);

    // ── LVGL core ──
    lv_init();

    // Small partial render buffers in PSRAM. With a single panel framebuffer
    // (use_psram=1 — required so the backlight rail doesn't brown out, see
    // lgfx_config.h), small partial flushes keep the CPU/DMA contention window
    // short, minimising the horizontal tearing inherent to a single live
    // framebuffer. Two buffers let LVGL render the next region while the previous
    // is pushed. RGB565 => 2 bytes per pixel.
    constexpr uint32_t kBufLines  = 80;
    constexpr uint32_t kBufPixels = 1024u * kBufLines;
    constexpr size_t   kBufBytes  = kBufPixels * sizeof(lv_color16_t);

    void* buf1 = heap_caps_malloc(kBufBytes, MALLOC_CAP_SPIRAM);
    void* buf2 = heap_caps_malloc(kBufBytes, MALLOC_CAP_SPIRAM);
    if (!buf1 || !buf2) {
        Serial.println("[lvgl] draw buffer alloc failed");
        while (true) delay(100);
    }

    // ── LVGL 9 display registration ──
    // Replaces LVGL 8's lv_disp_draw_buf_init + lv_disp_drv_init/register.
    lv_display_t* disp = lv_display_create(1024, 600);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_set_buffers(disp, buf1, buf2, kBufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

    // ── LVGL 9 input device registration ──
    // Replaces LVGL 8's lv_indev_drv_init/register.
    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvgl_touch_cb);

    // ── Build the dashboard UI ──
    dashboard::create();

    last_tick_ms_ = millis();
    Serial.println("[screen] LVGL 9.5 ready");
}

void MainScreenController::tick() {
    // Drain a payload buffered by the WiFi task.
    if (new_payload_) {
        Payload local;
        portENTER_CRITICAL(&payload_mux_);
        memcpy(&local, pending_payload_, sizeof(Payload));
        new_payload_ = false;
        portEXIT_CRITICAL(&payload_mux_);
        dashboard::update(local);
    }

    // Apply a buffered connection-status change.
    if (status_dirty_) {
        bool online;
        portENTER_CRITICAL(&payload_mux_);
        online        = status_online_;
        status_dirty_ = false;
        portEXIT_CRITICAL(&payload_mux_);
        dashboard::set_server_status(online);
    }

    // Advance LVGL by real elapsed time, then run its timers/rendering.
    uint32_t now_ms   = millis();
    uint32_t delta_ms = now_ms - last_tick_ms_;
    last_tick_ms_     = now_ms;
    lv_tick_inc(delta_ms);
    lv_timer_handler();
}

void MainScreenController::onPayload(const Payload& p) {
    portENTER_CRITICAL(&payload_mux_);
    memcpy(pending_payload_, &p, sizeof(Payload));
    new_payload_ = true;
    portEXIT_CRITICAL(&payload_mux_);
}

void MainScreenController::onConnectionChange(bool online) {
    Serial.printf("[display] server %s\n", online ? "online" : "offline");
    portENTER_CRITICAL(&payload_mux_);
    status_online_ = online;
    status_dirty_  = true;
    portEXIT_CRITICAL(&payload_mux_);
}
