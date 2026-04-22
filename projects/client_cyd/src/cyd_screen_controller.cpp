#include "cyd_screen_controller.h"
#include <string.h>

#ifndef UNIT_TEST
#include "lgfx_config.h"
#include <src/lvgl.h>
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

static void lvgl_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
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
      rpm_arc_(nullptr),
      rpm_label_(nullptr),
      speed_label_(nullptr),
      consumption_label_(nullptr),
      avg_consumption_label_(nullptr),
      distance_label_(nullptr),
      mil_indicator_(nullptr),
      server_status_label_(nullptr),
      last_tick_ms_(0),
      last_touch_ms_(0),
      touch_pressed_(false) {}

bool CYDScreenController::begin() {
#ifndef UNIT_TEST
    // Initialise ILI9341 + XPT2046 via LovyanGFX
    s_lgfx.init();
    s_lgfx.initDMA();           // Enable DMA for fast async buffer writes
    s_lgfx.setRotation(0);      // offset_rotation=4 in lgfx_config handles landscape
    s_lgfx.startWrite();        // Keep SPI CS active for DMA pushes

    // Enable secondary backlight power rail
    pinMode(GPIO_BL_EN, OUTPUT);
    digitalWrite(GPIO_BL_EN, HIGH);

    lv_init();

    // Register LVGL display driver
    lv_display_t* disp = lv_display_create(320, 240);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_set_buffers(disp, s_draw_buf_1, s_draw_buf_2,
                           sizeof(s_draw_buf_1), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_add_event_cb(disp, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    lv_obj_t* scr = lv_scr_act();

    // RPM arc
    rpm_arc_ = lv_arc_create(scr);
    lv_obj_set_size(rpm_arc_, 150, 150);
    lv_arc_set_range(rpm_arc_, 0, 6000);
    lv_arc_set_value(rpm_arc_, 0);
    lv_obj_align(rpm_arc_, LV_ALIGN_TOP_LEFT, 10, 10);

    rpm_label_ = lv_label_create(scr);
    lv_label_set_text(rpm_label_, "0 rpm");
    lv_obj_align_to(rpm_label_, rpm_arc_, LV_ALIGN_CENTER, 0, 0);

    speed_label_ = lv_label_create(scr);
    lv_label_set_text(speed_label_, "0 km/h");
    lv_obj_align(speed_label_, LV_ALIGN_TOP_MID, 0, 10);

    consumption_label_ = lv_label_create(scr);
    lv_label_set_text(consumption_label_, "0.0 km/L");
    lv_obj_align(consumption_label_, LV_ALIGN_LEFT_MID, 10, 0);

    avg_consumption_label_ = lv_label_create(scr);
    lv_label_set_text(avg_consumption_label_, "avg 0.0 km/L");
    lv_obj_align(avg_consumption_label_, LV_ALIGN_LEFT_MID, 10, 30);

    distance_label_ = lv_label_create(scr);
    lv_label_set_text(distance_label_, "0.0 km");
    lv_obj_align(distance_label_, LV_ALIGN_LEFT_MID, 10, 60);

    mil_indicator_ = lv_label_create(scr);
    lv_label_set_text(mil_indicator_, "MIL");
    lv_obj_set_style_text_color(mil_indicator_, lv_color_hex(0x888888), 0);
    lv_obj_align(mil_indicator_, LV_ALIGN_TOP_RIGHT, -10, 10);

    server_status_label_ = lv_label_create(scr);
    lv_label_set_text(server_status_label_, "SERVER OFFLINE");
    lv_obj_set_style_text_color(server_status_label_, lv_color_hex(0xFF0000), 0);
    lv_obj_align(server_status_label_, LV_ALIGN_BOTTOM_MID, 0, -10);
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
#ifndef UNIT_TEST
    // Called from Arduino loop task (same as tick()) — but guard anyway for safety
    portENTER_CRITICAL(&mux_);
    pending_status_     = online;
    has_pending_status_ = true;
    portEXIT_CRITICAL(&mux_);
#else
    (void)online;
#endif
}

void CYDScreenController::tick() {
#ifndef UNIT_TEST
    uint32_t now_ms   = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t delta_ms = now_ms - last_tick_ms_;
    last_tick_ms_     = now_ms;

    // Drain pending updates from WiFi-task callbacks — safe to call LVGL here (loop task)
    portENTER_CRITICAL(&mux_);
    bool     apply_payload = has_pending_payload_;
    Payload  payload_copy  = pending_payload_;
    bool     apply_status  = has_pending_status_;
    bool     status_copy   = pending_status_;
    has_pending_payload_   = false;
    has_pending_status_    = false;
    portEXIT_CRITICAL(&mux_);

    if (apply_payload) {
        char buf[32];
        lv_arc_set_value(rpm_arc_, payload_copy.rpm);
        snprintf(buf, sizeof(buf), "%u rpm", (unsigned)payload_copy.rpm);
        lv_label_set_text(rpm_label_, buf);

        snprintf(buf, sizeof(buf), "%u km/h", (unsigned)payload_copy.speed_kmh);
        lv_label_set_text(speed_label_, buf);

        snprintf(buf, sizeof(buf), "%.1f km/L", payload_copy.consumption_km_per_l);
        lv_label_set_text(consumption_label_, buf);

        snprintf(buf, sizeof(buf), "avg %.1f km/L", payload_copy.avg_consumption_km_per_l);
        lv_label_set_text(avg_consumption_label_, buf);

        snprintf(buf, sizeof(buf), "%.1f km", payload_copy.distance_km);
        lv_label_set_text(distance_label_, buf);

        lv_obj_set_style_text_color(mil_indicator_,
            payload_copy.mil_on ? lv_color_hex(0xFF8000) : lv_color_hex(0x888888), 0);
    }

    if (apply_status) {
        if (status_copy) {
            lv_label_set_text(server_status_label_, "SERVER ONLINE");
            lv_obj_set_style_text_color(server_status_label_, lv_color_hex(0x00FF00), 0);
        } else {
            lv_label_set_text(server_status_label_, "SERVER OFFLINE");
            lv_obj_set_style_text_color(server_status_label_, lv_color_hex(0xFF0000), 0);
        }
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
