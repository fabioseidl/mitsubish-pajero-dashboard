#include "app_ui.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// SquareLine Studio generated UI (LVGL 9 export). Do NOT edit anything under
// src/ui/ — it is regenerated on every SquareLine export. ui.h declares ui_init()
// and the ui_lb* label handles used below.
#include "ui/ui.h"

namespace app_ui {

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

void create() {
    // Builds the widget tree and loads ui_main as the active screen.
    // Must run after lv_init() and after the LVGL display is registered.
    ui_init();
}

void update(const Payload& p) {
    if (ui_lbrpm == nullptr) return;                   // create() not called yet
    if (!(p.flags & PAYLOAD_FLAG_DATA_VALID)) return;

    set_if_changed(ui_lbrpm,                "%u",   (unsigned)p.rpm);
    set_if_changed(ui_lbfuelrate,           "%.1f", p.fuel_rate_l_per_h);
    // Economy readout is capped at 99 km/l — anything higher shows "99+".
    if (p.consumption_km_per_l > 99.0f)
        set_if_changed(ui_lbconsumptionkml, "99+");
    else
        set_if_changed(ui_lbconsumptionkml, "%.1f", p.consumption_km_per_l);
    set_if_changed(ui_lbavgconsumptionkml,  "%.1f", p.avg_consumption_km_per_l);
    set_if_changed(ui_lbdistancekm,         "%.1f", p.distance_km);
    set_if_changed(ui_lbbarometerpressure,  "%u",   (unsigned)p.baro_pressure_kpa);
    set_if_changed(ui_lbambientetemperature,"%.0f", p.ambient_temp_c);
    set_if_changed(ui_lbboostpressure,      "%.1f", p.boost_pres);
    set_if_changed(ui_lbengineload,         "%.0f", p.engine_load_pct);
    set_if_changed(ui_lbthrottle,           "%.0f", p.throttle_pct);
    set_if_changed(ui_lbcoolanttemp,        "%.0f", p.coolant_temp_c);
    set_if_changed(ui_lboiltemp,            "%.0f", p.oil_temp_c);
    set_if_changed(ui_lbdpfsoot,            "%.0f", p.dpf_soot_load);
    // ui_lbaltitude is driven by GPS altitude (set_gps_altitude), not the OBD
    // barometric altitude — see set_gps_* below.
    set_if_changed(ui_lbvoltage,            "%.1f", p.module_voltage_v);

    // Two gear readouts on the new screen: current AT gear and AT target gear.
    // Both decode with the same gear table — swap the sources here if the labels
    // should show different values.
    char gbuf[8];
    set_if_changed(ui_lbatgearposition, "%s", gear_text(p.at_gear_pos,    gbuf, sizeof(gbuf)));
    set_if_changed(ui_lbgearposition,   "%s", gear_text(p.at_target_gear, gbuf, sizeof(gbuf)));
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
    set_text(ui_lbdatetime, hhmm);
}
void set_gps_altitude(const char* text) { set_text(ui_lbaltitude, text); }
void set_gps_compass(const char* text)  { set_text(ui_lbcompass,  text); }
void set_trip_time(const char* text)    { set_text(ui_lbtriptime, text); }
void set_ambient_temperature(const char* text) { set_text(ui_lbambienttemperature, text); }

void set_server_status(bool online) {
    if (ui_lbserverstatus == nullptr) return;
    // Only recolour the status indicator (its text is left as designed):
    // green when ONLINE, red when OFFLINE.
    lv_obj_set_style_text_color(
        ui_lbserverstatus,
        online ? lv_color_hex(0x29CC5B) : lv_color_hex(0xE03B3B),
        LV_PART_MAIN | LV_STATE_DEFAULT);
}

}  // namespace app_ui
