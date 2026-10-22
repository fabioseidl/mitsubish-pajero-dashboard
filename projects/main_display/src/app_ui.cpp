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
#include "pid_map.h"   // AUTOAC_TEMP_LO / _HI
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

// Readout geometry: 171x32, centred in the header over the status pill while shown.
constexpr int16_t BL_BTN_W = 171;
constexpr int16_t BL_BTN_H = 32;
constexpr int16_t BL_BTN_X = 426;
constexpr int16_t BL_BTN_Y = 8;

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

// Set a bar's indicator colour, skipping no-op writes.
static void set_indicator_color(lv_obj_t* bar, uint32_t hex) {
    const lv_color_t c = lv_color_hex(hex);
    if (!lv_color_eq(lv_obj_get_style_bg_color(bar, LV_PART_INDICATOR), c)) {
        lv_obj_set_style_bg_color(bar, c, LV_PART_INDICATOR);
    }
}

// Set a bar's value clamped to its range; lv_bar_set_value is itself a no-op when unchanged.
static void set_bar(lv_obj_t* bar, float v) {
    const int32_t max = lv_bar_get_max_value(bar);
    int32_t i = (int32_t)lroundf(v);
    lv_bar_set_value(bar, i < 0 ? 0 : i > max ? max : i, LV_ANIM_OFF);
}

void update(const Payload& p) {
    using namespace dashboard_view;
    if (s_w.rpm == nullptr) return;                   // create() not called yet
    if (!(p.flags & PAYLOAD_FLAG_DATA_VALID)) return;

    // ── Engine panel ──
    set_if_changed(s_w.speed, "%u", (unsigned)payload_speed_kmh_u8(&p));
    set_if_changed(s_w.rpm,   "%u", (unsigned)p.rpm);
    set_bar(s_w.rpm_bar, p.rpm);
    // Green to 3500, amber to the 4000 redline, red past it.
    set_indicator_color(s_w.rpm_bar, p.rpm >= 4000 ? color::kOffline : p.rpm >= 3500 ? color::kWarn : color::kOnline);
    // Boost is derived from MAP - ambient, both whole-kPa, so the data resolves to
    // ~0.01 bar. Two decimals show that; one would hide all movement below 0.1 bar.
    set_if_changed(s_w.boost, "%.2f bar", p.boost_pres);
    set_bar(s_w.boost_bar, p.boost_pres * 100.0f);
    set_if_changed(s_w.load, "%.0f %%", p.engine_load_pct);
    set_bar(s_w.load_bar, p.engine_load_pct);
    set_if_changed(s_w.throttle, "%.0f %%", p.throttle_pct);
    set_bar(s_w.throttle_bar, p.throttle_pct);

    setGear(s_w, (int)lroundf(p.at_gear_pos), (int)lroundf(p.at_target_gear));

    // ── Tiles ──
    // Economy readout is capped at 99 km/l — anything higher shows "99+".
    if (p.consumption_km_per_l > 99.0f) set_if_changed(s_w.consumption.value, "99+");
    else                                set_if_changed(s_w.consumption.value, "%.1f", p.consumption_km_per_l);
    set_if_changed(s_w.avg_consumption.value, "%.1f", p.avg_consumption_km_per_l);
    set_if_changed(s_w.fuel_rate.value,       "%.1f", p.fuel_rate_l_per_h);
    // Whole km from 1000 up, so value and unit still fit the tile.
    set_if_changed(s_w.distance.value, p.distance_km >= 1000.0f ? "%.0f" : "%.1f", p.distance_km);
    // Trip time is the server's engine-on counter (Payload.trip_time_s), not this
    // client's uptime: it survives the server's deep sleep and resets with the
    // rest of the trip totals, so it stays consistent with TRIP km.
    set_if_changed(s_w.trip_time.value, "%u:%02u",
                   (unsigned)(p.trip_time_s / 3600u), (unsigned)((p.trip_time_s % 3600u) / 60u));

    // Coolant strip: blue < 60 °C (cold, go easy), green to 100 °C (thermostat open, ~85-95
    // when warm), yellow to 105 °C (hard work: towing, climbs), red above (overheating).
    set_if_changed(s_w.coolant.value, "%.0f", p.coolant_temp_c);
    setBgColor(s_w.coolant.strip, p.coolant_temp_c < 60.0f   ? color::kCold
                                : p.coolant_temp_c <= 100.0f ? color::kOnline
                                : p.coolant_temp_c <= 105.0f ? color::kWarn : color::kOffline);
    set_if_changed(s_w.iat.value, "%.0f", p.intake_air_temp_c);
    // Battery strip: red outside 12.2..15.0 V, amber below 13.2 V (not charging), green otherwise.
    set_if_changed(s_w.voltage.value, "%.1f", p.module_voltage_v);
    setBgColor(s_w.voltage.strip, (p.module_voltage_v < 12.2f || p.module_voltage_v > 15.0f) ? color::kOffline
                                : p.module_voltage_v < 13.2f ? color::kWarn : color::kOnline);
    set_if_changed(s_w.catalyst.value, "%.0f", p.catalyst_temp_c);

    // ── Status pill ──
    // Engine fault: red with the MIL lit, amber when codes are stored but the lamp is off.
    const uint32_t fault = p.mil_on ? color::kOffline : color::kWarn;
    setLit(s_w.engine, p.mil_on || p.dtc_count > 0, fault);
    set_if_changed(s_w.dtc, "%u", (unsigned)p.dtc_count);
    setLit(s_w.dtc, true, fault);
    setHidden(s_w.dtc, p.dtc_count == 0);
    setLamps(s_w, p.body_flags & PAYLOAD_BODY_HEAD_LAMP_LO, p.body_flags & PAYLOAD_BODY_HEAD_LAMP_HI);
    setLit(s_w.door, p.body_flags & (PAYLOAD_BODY_DRIVER_DOOR | PAYLOAD_BODY_OTHER_DOOR), color::kOffline);
    setLit(s_w.turn_l, p.body_flags & PAYLOAD_BODY_LEFT_TURN, color::kOnline);
    setLit(s_w.turn_r, p.body_flags & PAYLOAD_BODY_RIGHT_TURN, color::kOnline);

    // ── Climate pill ──
    // Fan step 0 = system off; the compressor (A/C bit) can be off while the fan runs.
    const uint8_t fan     = p.climate_fan > 15 ? 15 : p.climate_fan;
    const bool    running = fan > 0;
    const uint8_t cf      = p.climate_flags;
    setLit(s_w.ac, running && (cf & PAYLOAD_CLIMATE_AC_ON), color::kCold);
    if      (p.climate_set_temp_c == AUTOAC_TEMP_LO) set_if_changed(s_w.set_temp, "LO");
    else if (p.climate_set_temp_c == AUTOAC_TEMP_HI) set_if_changed(s_w.set_temp, "HI");
    else                                             set_if_changed(s_w.set_temp, "%u C", (unsigned)p.climate_set_temp_c);
    setLit(s_w.set_temp, running);
    setLit(s_w.fan, running);
    setFanBars(s_w, (fan + 2) / 3);   // 5 bars, 3 fan steps each
    setLit(s_w.defrost, running && (cf & PAYLOAD_CLIMATE_VENT_WIND), color::kWarn);
    setLit(s_w.seat, running && (cf & (PAYLOAD_CLIMATE_VENT_FACE | PAYLOAD_CLIMATE_VENT_FEET)));
    setHidden(s_w.vent_face, !(running && (cf & PAYLOAD_CLIMATE_VENT_FACE)));
    setHidden(s_w.vent_feet, !(running && (cf & PAYLOAD_CLIMATE_VENT_FEET)));
    setLit(s_w.recirc, running && (cf & PAYLOAD_CLIMATE_RECIRC), color::kOnline);
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
    char hhmm[6] = "--:--";
    if (text != nullptr && strlen(text) >= 16) {
        memcpy(hhmm, text + 11, 5);   // chars 11..15 = "HH:MM"
        hhmm[5] = '\0';
    }
    set_text(s_w.datetime, hhmm);
}

void set_gps_altitude(const char* text) { set_text(s_w.altitude.value, text); }

void set_gps_compass(const char* text) {
    if (s_w.heading == nullptr) return;   // create() not called yet

    // Course over ground in degrees, or "--" without a fix.
    float deg = 0.0f;
    if (text == nullptr || sscanf(text, "%f", &deg) != 1) {
        set_text(s_w.heading, "--");
        set_text(s_w.cardinal, "NO FIX");
        deg = -1.0f;
    } else {
        deg = fmodf(deg, 360.0f);                   // normalise to [0, 360)
        if (deg < 0.0f) deg += 360.0f;
        const int whole = (int)lroundf(deg) % 360;
        set_if_changed(s_w.heading, "%03d", whole);

        // 16-point cardinal: each sector spans 22.5°, centred on its bearing.
        static const char* const kCardinals[16] = {
            "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
            "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW" };
        set_text(s_w.cardinal, kCardinals[((int)lroundf(deg / 22.5f)) % 16]);
        deg = (float)whole;
    }

    // Rotating the card redraws the whole dial, so only on a whole-degree change.
    static int last = -2;
    const int now = (int)deg;
    if (now != last) {
        last = now;
        dashboard_view::setHeading(s_w, now);
    }
}

void set_ambient_temperature(const char* text) { set_text(s_w.cabin_temp, text); }

void set_server_status(bool online) {
    if (s_w.link_dot == nullptr) return;
    dashboard_view::setBgColor(s_w.link_dot, online ? dashboard_view::color::kOnline
                                                    : dashboard_view::color::kOffline);
}

}  // namespace app_ui
