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
    if (ui_lbspeed == nullptr) return;                 // create() not called yet
    if (!(p.flags & PAYLOAD_FLAG_DATA_VALID)) return;

    set_if_changed(ui_lbspeed,              "%u",   (unsigned)p.speed_kmh);
    set_if_changed(ui_lbrpm,                "%u",   (unsigned)p.rpm);
    set_if_changed(ui_lbfuelrate,           "%.1f", p.fuel_rate_l_per_h);
    set_if_changed(ui_lbconsumptionkml,     "%.1f", p.consumption_km_per_l);
    set_if_changed(ui_lbavgconsumptionkml,  "%.1f", p.avg_consumption_km_per_l);
    set_if_changed(ui_lbdistancekm,         "%.1f", p.distance_km);
    set_if_changed(ui_lbbarometerpressure,  "%u",   (unsigned)p.baro_pressure_kpa);
    set_if_changed(ui_lbambientetemperature,"%.0f", p.ambient_temp_c);
    set_if_changed(ui_lbboostpressure,      "%.1f", p.boost_pres);
    set_if_changed(ui_lbengineload,         "%.0f", p.engine_load_pct);
    set_if_changed(ui_lbthrottle,           "%.0f", p.throttle_pct);
    set_if_changed(ui_lbcoolanttemp,        "%.0f", p.coolant_temp_c);
    set_if_changed(ui_lboiltemp,            "%.0f", p.oil_temp_c);
    set_if_changed(ui_lbatftemp,            "%.0f", p.at_atf_temp_c);
    set_if_changed(ui_lbdpfsoot,            "%.0f", p.dpf_soot_load);
    set_if_changed(ui_lbaltitude,           "%.0f", p.altitude_m);
    set_if_changed(ui_lbvoltage,            "%.1f", p.module_voltage_v);

    // Two gear readouts on the new screen: current AT gear and AT target gear.
    // Both decode with the same gear table — swap the sources here if the labels
    // should show different values.
    char gbuf[8];
    set_if_changed(ui_lbatgearposition, "%s", gear_text(p.at_gear_pos,    gbuf, sizeof(gbuf)));
    set_if_changed(ui_lbgearposition,   "%s", gear_text(p.at_target_gear, gbuf, sizeof(gbuf)));
}

void set_server_status(bool /*online*/) {
    // The SquareLine screen has no server-status widget yet. Kept so main.cpp's
    // ServerConnectionMonitor wiring stays intact; add a status label in
    // SquareLine and surface it here to get an ONLINE/OFFLINE indicator back.
}

}  // namespace app_ui
