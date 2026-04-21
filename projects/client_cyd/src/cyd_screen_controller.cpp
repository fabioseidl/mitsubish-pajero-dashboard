#include "cyd_screen_controller.h"
#include <string.h>

#ifndef UNIT_TEST
#include <lvgl.h>
#include <esp_timer.h>
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
      last_tick_ms_(0) {}

bool CYDScreenController::begin() {
#ifndef UNIT_TEST
    lv_init();

    // Display driver and input driver registration would go here
    // (ILI9341 flush callback + XPT2046 read callback)

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
    char buf[32];

    lv_arc_set_value(rpm_arc_, payload.rpm);
    snprintf(buf, sizeof(buf), "%u rpm", (unsigned)payload.rpm);
    lv_label_set_text(rpm_label_, buf);

    snprintf(buf, sizeof(buf), "%u km/h", (unsigned)payload.speed_kmh);
    lv_label_set_text(speed_label_, buf);

    snprintf(buf, sizeof(buf), "%.1f km/L", payload.consumption_km_per_l);
    lv_label_set_text(consumption_label_, buf);

    snprintf(buf, sizeof(buf), "avg %.1f km/L", payload.avg_consumption_km_per_l);
    lv_label_set_text(avg_consumption_label_, buf);

    snprintf(buf, sizeof(buf), "%.1f km", payload.distance_km);
    lv_label_set_text(distance_label_, buf);

    lv_obj_set_style_text_color(mil_indicator_,
        payload.mil_on ? lv_color_hex(0xFF8000) : lv_color_hex(0x888888), 0);
#else
    (void)payload;
#endif
}

void CYDScreenController::onServerStatusChanged(bool online) {
#ifndef UNIT_TEST
    if (online) {
        lv_label_set_text(server_status_label_, "SERVER ONLINE");
        lv_obj_set_style_text_color(server_status_label_, lv_color_hex(0x00FF00), 0);
    } else {
        lv_label_set_text(server_status_label_, "SERVER OFFLINE");
        lv_obj_set_style_text_color(server_status_label_, lv_color_hex(0xFF0000), 0);
    }
#else
    (void)online;
#endif
}

void CYDScreenController::tick() {
#ifndef UNIT_TEST
    uint32_t now_ms   = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t delta_ms = now_ms - last_tick_ms_;
    last_tick_ms_     = now_ms;

    lv_tick_inc(delta_ms);
    lv_timer_handler();

    // XPT2046 touch poll would go here; call brightness_.onTouch() on press
#endif
}
