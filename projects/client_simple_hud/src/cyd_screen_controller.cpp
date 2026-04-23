#include "cyd_screen_controller.h"
#include <string.h>

#ifndef UNIT_TEST
#include <Arduino.h>
#include "lgfx_config.h"
#include <src/lvgl.h>
#include "ui.h"
#include <esp_timer.h>

static LGFX s_lgfx;

// Double-buffered, 10-line partial render buffers aligned for DMA
static uint8_t s_draw_buf_1[320 * 10 * 2] __attribute__((aligned(32)));
static uint8_t s_draw_buf_2[320 * 10 * 2] __attribute__((aligned(32)));

static void rounder_event_cb(lv_event_t* e) {
    lv_area_t* area = lv_event_get_invalidated_area(e);
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 |= 1;
    area->y2 |= 1;
}

static bool s_flush_logged = false;

static void lvgl_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    if (!s_flush_logged) {
        Serial.printf("[SCREEN] First flush: area=(%d,%d)-(%d,%d)\n",
                      area->x1, area->y1, area->x2, area->y2);
        s_flush_logged = true;
    }
    if (s_lgfx.getStartCount() == 0) {
        s_lgfx.endWrite();
    }
    s_lgfx.pushImageDMA(area->x1, area->y1,
                        area->x2 - area->x1 + 1,
                        area->y2 - area->y1 + 1,
                        (uint16_t*)px_map);
    lv_display_flush_ready(disp);
}
#endif

CYDScreenController::CYDScreenController(BrightnessController& brightness)
    : brightness_(brightness),
      last_tick_ms_(0),
      last_touch_ms_(0),
      touch_pressed_(false) {}

bool CYDScreenController::begin() {
#ifndef UNIT_TEST
    Serial.println("[SCREEN] begin()");

    // Initialise ILI9341 + XPT2046 via LovyanGFX
    s_lgfx.init();
    s_lgfx.initDMA();           // Enable DMA for fast async buffer writes
    s_lgfx.setRotation(0);      // offset_rotation=4 in lgfx_config handles landscape
    s_lgfx.startWrite();        // Keep SPI CS active for DMA pushes

    // Enable secondary backlight power rail
    pinMode(GPIO_BL_EN, OUTPUT);
    digitalWrite(GPIO_BL_EN, HIGH);
    Serial.println("[SCREEN] LGFX + backlight OK");

    lv_init();
    Serial.println("[SCREEN] lv_init OK");

    // Register LVGL display driver
    lv_display_t* disp = lv_display_create(320, 240);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_set_buffers(disp, s_draw_buf_1, s_draw_buf_2,
                           sizeof(s_draw_buf_1), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_add_event_cb(disp, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    Serial.println("[SCREEN] LVGL display registered");

    ui_init();
    Serial.println("[SCREEN] UI init OK");
#endif
    return true;
}

void CYDScreenController::onPayloadReceived(const Payload& payload) {
#ifndef UNIT_TEST
    // Called from WiFi task (CPU 0) — only buffer here; LVGL update happens in tick()
    portENTER_CRITICAL(&mux_);
    pending_payload_     = payload;
    has_pending_payload_ = true;
    portEXIT_CRITICAL(&mux_);
#else
    (void)payload;
#endif
}

void CYDScreenController::onServerStatusChanged(bool online) {
    if (online) {
        lv_obj_add_flag(ui_iconserverdisconected, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(ui_iconserverdisconected, LV_OBJ_FLAG_HIDDEN);
    }
}

void CYDScreenController::tick() {
#ifndef UNIT_TEST
    uint32_t now_ms   = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t delta_ms = now_ms - last_tick_ms_;
    last_tick_ms_     = now_ms;

    // Drain pending payload from WiFi-task callback — safe to call LVGL here (loop task)
    portENTER_CRITICAL(&mux_);
    bool    apply_payload = has_pending_payload_;
    Payload payload_copy  = pending_payload_;
    has_pending_payload_  = false;
    portEXIT_CRITICAL(&mux_);

    if (apply_payload) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u", (unsigned)payload_copy.speed_kmh);
        lv_label_set_text(ui_lbSpeed, buf);

        snprintf(buf, sizeof(buf), "%u", (unsigned)payload_copy.rpm);
        lv_label_set_text(ui_lbrpm, buf);
        lv_bar_set_value(ui_barrpm, payload_copy.rpm, LV_ANIM_ON);

        snprintf(buf, sizeof(buf), "%.1f", (float)payload_copy.avg_consumption_km_per_l);
        lv_label_set_text(ui_lbavgconsumption, buf);
    }

    lv_tick_inc(delta_ms);
    lv_timer_handler();

    // XPT2046 touch poll — advance brightness on each new press
    int16_t tx = 0, ty = 0;
    bool touched = s_lgfx.getTouch(&tx, &ty);
    if (touched) {
        if (!touch_pressed_) {
            if (now_ms - last_touch_ms_ >= 300) {
                brightness_.onTouch();
                last_touch_ms_ = now_ms;
            }
            touch_pressed_ = true;
        }
    } else {
        touch_pressed_ = false;
    }
#endif
}
