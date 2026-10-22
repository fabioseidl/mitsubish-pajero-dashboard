#include "app_ui.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "dashboard_view.h"

#include "i_display.h"
#include "step_brightness.h"

namespace app_ui {
namespace {

// ── Backlight control ────────────────────────────────────────────────────────
//
// This board CANNOT dim its backlight. It is PCF8574 pin P1 — a digital latch,
// HIGH = ON (see the PCF8574 map in
// .claude/skills/client-firmware/references/main_display.md), with no PWM path
// and no backlight GPIO on the ESP32 at all. (GPIO 0 is not an option either: it is RGB Green 3, so the BOOT
// button cannot be read while the panel runs.) So unlike main_hud, where the
// identical 10-step cycle drives LEDC PWM on GPIO 1, "brightness" here is the
// opacity of a black layer drawn over the UI: perceived brightness follows the
// same 10 steps, actual backlight power does not change. Software-PWM'ing P1 was
// rejected — it would flicker under LVGL/WiFi load and fight for the I2C bus
// shared with the GT911 touch, MPU6050 and AHT20/BMP280.
//
// The backlight itself stays latched ON; main.cpp's 1 s 0xFF re-assert is
// untouched and nothing here contends with it.
//
// Both widgets live on lv_layer_top() so they survive a screen load; the button
// is created *before* the dim overlay so the overlay covers it and it dims along
// with everything else, rather than staying glaringly lit at night.

// Readout geometry: 171x32, top-left, just under the header bar.
constexpr int16_t BL_BTN_W = 171;
constexpr int16_t BL_BTN_H = 32;
constexpr int16_t BL_BTN_X = 13;
constexpr int16_t BL_BTN_Y = 52;

// The readout hides this long after the last press, leaving the dashboard clean.
// Shown at boot so the current level can be seen without pressing anything.
constexpr uint32_t BL_IDLE_HIDE_MS = 3000;

lv_obj_t*   s_dim_overlay     = nullptr;
lv_obj_t*   s_backlight_btn   = nullptr;
lv_obj_t*   s_backlight_label = nullptr;
lv_timer_t* s_hide_timer      = nullptr;

dashboard_view::Widgets s_w{};

class OverlayDimmer : public IDisplay {
public:
    bool begin() override { return true; }

    void setBacklightPercent(uint8_t percent) override {
        if (s_dim_overlay == nullptr) return;
        if (percent > 100) percent = 100;
        // 100% → fully transparent (nothing over the UI); 10% → nearly opaque.
        const uint8_t alpha = (uint8_t)(((100 - (uint16_t)percent) * 255) / 100);
        lv_obj_set_style_bg_opa(s_dim_overlay, alpha, LV_PART_MAIN);
#ifdef ARDUINO
        Serial.printf("[BACKLIGHT] %3u%%  overlay alpha=%u\n", percent, alpha);
#endif
    }
};

OverlayDimmer  s_dimmer;
StepBrightness s_brightness(s_dimmer);

void refresh_backlight_label() {
    if (s_backlight_label == nullptr) return;
    lv_label_set_text_fmt(s_backlight_label, "%u%%", s_brightness.getCurrentPercent());
}

// Fires once BL_IDLE_HIDE_MS after the last press, then parks itself. A repeating
// timer that pauses is used rather than a one-shot: LVGL deletes a one-shot after
// it runs, and this one has to be reusable for every future press.
void hide_timer_cb(lv_timer_t* t) {
    if (s_backlight_btn) lv_obj_add_flag(s_backlight_btn, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(t);
}

void show_backlight_btn() {
    if (s_backlight_btn == nullptr) return;
    lv_obj_remove_flag(s_backlight_btn, LV_OBJ_FLAG_HIDDEN);
    if (s_hide_timer) {
        lv_timer_reset(s_hide_timer);   // restart the countdown on every press
        lv_timer_resume(s_hide_timer);
    }
}

void on_backlight_clicked(lv_event_t* e) {
    LV_UNUSED(e);
    cycle_backlight();
}

void build_backlight_control() {
    // Button first — see the note above: the overlay is added after so that it
    // sits on top and dims the button too.
    s_backlight_btn = lv_button_create(lv_layer_top());
    lv_obj_set_size(s_backlight_btn, BL_BTN_W, BL_BTN_H);
    lv_obj_align(s_backlight_btn, LV_ALIGN_TOP_LEFT, BL_BTN_X, BL_BTN_Y);
    lv_obj_set_style_bg_color(s_backlight_btn, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_backlight_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_backlight_btn, lv_color_hex(0x606060), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_backlight_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_backlight_btn, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(s_backlight_btn, on_backlight_clicked, LV_EVENT_CLICKED, NULL);

    s_backlight_label = lv_label_create(s_backlight_btn);
    lv_obj_set_style_text_font(s_backlight_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_backlight_label, lv_color_hex(0xC0C0C0), LV_PART_MAIN);
    lv_obj_center(s_backlight_label);

    // The dim layer lives on lv_layer_top() rather than on the screen: the top
    // layer sits above every screen and survives a screen load, so the chosen
    // level cannot be undone by the UI being rebuilt. It is click-through, so
    // it never swallows a touch meant for the button or the widgets underneath.
    s_dim_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_dim_overlay);
    lv_obj_set_size(s_dim_overlay,
                    lv_display_get_horizontal_resolution(NULL),
                    lv_display_get_vertical_resolution(NULL));
    lv_obj_set_pos(s_dim_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_dim_overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_dim_overlay, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_remove_flag(s_dim_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_dim_overlay, LV_OBJ_FLAG_SCROLLABLE);

    s_brightness.applyCurrent();  // 50% at boot
    refresh_backlight_label();

    // Visible at boot, then hidden 3 s later — same as show_backlight_btn() does
    // after every press.
    s_hide_timer = lv_timer_create(hide_timer_cb, BL_IDLE_HIDE_MS, NULL);
    show_backlight_btn();
}

}  // namespace

// Update a label only when its rendered text actually changes. Every redraw on
// the single-framebuffer RGB panel is a potential tear, so skipping no-op writes
// reduces visible flicker.
static void set_if_changed(lv_obj_t* label, const char* fmt, ...) {
    if (label == nullptr) return;
    char buf[24];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (strcmp(lv_label_get_text(label), buf) != 0) {
        lv_label_set_text(label, buf);
    }
}

// Decode a Mitsubishi AT gear code (CAN 0x218 D2 nibble):
//   0 = N, 1..5 = forward gears, 11 = R, 13 = P, anything else = "-".
static const char* gear_text(float raw, char* buf, size_t n) {
    int g = (int)lroundf(raw);
    switch (g) {
        case 0:  return "N";
        case 11: return "R";
        case 13: return "P";
        default:
            if (g >= 1 && g <= 5) {
                snprintf(buf, n, "%d", g);
                return buf;
            }
            return "-";
    }
}

void cycle_backlight() {
    s_brightness.next();
    refresh_backlight_label();
    show_backlight_btn();  // reveal the readout and restart the 3 s countdown
}

void create() {
    // Must run after lv_init() and after the LVGL display is registered.
    s_w = dashboard_view::build(lv_screen_active());
    build_backlight_control();
}

void update(const Payload& p) {
    if (s_w.rpm == nullptr) return;                   // create() not called yet
    if (!(p.flags & PAYLOAD_FLAG_DATA_VALID)) return;

    set_if_changed(s_w.speed,              "%u",   (unsigned)payload_speed_kmh_u8(&p));
    set_if_changed(s_w.rpm,                "%u",   (unsigned)p.rpm);
    set_if_changed(s_w.fuel_rate,          "%.1f", p.fuel_rate_l_per_h);
    // Economy readout is capped at 99 km/l — anything higher shows "99+".
    if (p.consumption_km_per_l > 99.0f)
        set_if_changed(s_w.consumption, "99+");
    else
        set_if_changed(s_w.consumption, "%.1f", p.consumption_km_per_l);
    set_if_changed(s_w.avg_consumption,  "%.1f", p.avg_consumption_km_per_l);
    set_if_changed(s_w.distance,         "%.1f", p.distance_km);
    // Trip time is the server's engine-on counter (Payload.trip_time_s), not this
    // client's uptime: it survives the server's deep sleep and resets with the
    // rest of the trip totals, so it stays consistent with TRIP km.
    set_if_changed(s_w.trip_time,           "%02u:%02u:%02u",
                   (unsigned)(p.trip_time_s / 3600u),
                   (unsigned)((p.trip_time_s % 3600u) / 60u),
                   (unsigned)(p.trip_time_s % 60u));
    // Boost is derived from MAP - ambient, both whole-kPa, so the data resolves to
    // ~0.01 bar. Two decimals show that; "%.1f" quantised it to 0.1 bar steps and
    // hid all movement below a tenth of a bar.
    set_if_changed(s_w.boost,      "%.2f", p.boost_pres);
    set_if_changed(s_w.engine_load,         "%.0f", p.engine_load_pct);
    set_if_changed(s_w.coolant,        "%.0f", p.coolant_temp_c);
    // Coolant status dot: blue < 70 °C (cold), green 70–90 °C (normal),
    // red > 90 °C (hot). Only restyle on a bucket change to avoid needless
    // redraws (flicker) on the single-framebuffer panel.
    if (s_w.coolant_status != nullptr) {
        int bucket = (p.coolant_temp_c < 70.0f) ? 0
                   : (p.coolant_temp_c > 90.0f) ? 2
                                                : 1;
        static int last_bucket = -1;
        if (bucket != last_bucket) {
            last_bucket = bucket;
            static const uint32_t kColors[3] = { 0x3B8EE0, 0x29CC5B, 0xE03B3B };
            lv_obj_set_style_text_color(s_w.coolant_status, lv_color_hex(kColors[bucket]),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    // The altitude label is GPS altitude (set_gps_altitude), not the OBD baro altitude.
    set_if_changed(s_w.voltage,            "%.1f", p.module_voltage_v);

    // Two gear readouts on the new screen: current AT gear and AT target gear.
    // Both decode with the same gear table — swap the sources here if the labels
    // should show different values.
    char gbuf[8];
    set_if_changed(s_w.gear, "%s", gear_text(p.at_gear_pos,    gbuf, sizeof(gbuf)));
    set_if_changed(s_w.target_gear,   "%s", gear_text(p.at_target_gear, gbuf, sizeof(gbuf)));
}

// Set a label from a pre-formatted string, skipping no-op writes (every redraw
// on the single-framebuffer RGB panel is a potential tear).
static void set_text(lv_obj_t* label, const char* text) {
    if (label == nullptr || text == nullptr) return;
    if (strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

void set_gps_datetime(const char* text) {
    // GPS supplies "YYYY-MM-DD HH:MM:SS" (UTC-3); the label shows only "HH:MM".
    char hhmm[6] = "--";
    if (text != nullptr && strlen(text) >= 16) {
        memcpy(hhmm, text + 11, 5);   // chars 11..15 = "HH:MM"
        hhmm[5] = '\0';
    }
    set_text(s_w.datetime, hhmm);
}
void set_gps_altitude(const char* text) { set_text(s_w.altitude, text); }
void set_gps_compass(const char* text) {
    set_text(s_w.compass, text);

    // Derive the 16-point cardinal from the course-over-ground value shown in
    // s_w.compass ("--" when there is no fix). The arrow icon stays static.
    float deg = 0.0f;
    if (text == nullptr || sscanf(text, "%f", &deg) != 1) {
        set_text(s_w.compass_cardinal, "--");
        return;
    }
    deg = fmodf(deg, 360.0f);                   // normalise to [0, 360)
    if (deg < 0.0f) deg += 360.0f;

    // 16-point cardinal: each sector spans 22.5°, centred on its bearing.
    static const char* const kCardinals[16] = {
        "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW" };
    int idx = ((int)lroundf(deg / 22.5f)) % 16;
    set_text(s_w.compass_cardinal, kCardinals[idx]);
}
void set_ambient_temperature(const char* text) { set_text(s_w.cabin_temp, text); }
void set_humidity(const char* text) { set_text(s_w.humidity, text); }

/** Show v with one decimal, or "--" when it is NAN (sensor absent / link down). */
static void set_axis(lv_obj_t* label, float v) {
    if (isnan(v)) set_if_changed(label, "%s", "--");
    else          set_if_changed(label, "%.1f", v);
}

void set_imu(float ax, float ay, float az, float gx, float gy, float gz) {
    LV_UNUSED(gx); LV_UNUSED(gy); LV_UNUSED(gz);  // the layout has no gyro readout
    if (s_w.axis_x == nullptr) return;   // create() not called yet
    set_axis(s_w.axis_x,     ax);
    set_axis(s_w.axis_y,     ay);
    set_axis(s_w.axis_z,     az);
    // Net linear acceleration magnitude (m/s^2). The server removes gravity from
    // each axis, so this reads ~0 at rest and rises only under real motion.
    set_axis(s_w.acceleration, sqrtf(ax * ax + ay * ay + az * az));
}

void set_server_status(bool online) {
    if (s_w.server_status == nullptr) return;
    // Only recolour the status indicator (its text is left as designed):
    // green when ONLINE, red when OFFLINE.
    lv_obj_set_style_text_color(
        s_w.server_status,
        online ? lv_color_hex(0x29CC5B) : lv_color_hex(0xE03B3B),
        LV_PART_MAIN | LV_STATE_DEFAULT);
}

}  // namespace app_ui
