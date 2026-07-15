#pragma once

#include "payload.h"

// Bridge between the app and the SquareLine Studio generated interface in
// src/ui/ (an LVGL 9 export). Same three-function contract the old dashboard_ui
// exposed, so main.cpp stays decoupled from the widget tree:
//   - create()            builds the screen and loads it (call once, after
//                         lv_init() and the LVGL display is registered)
//   - update()            pushes a received Payload into the generated labels
//   - set_server_status() ONLINE/OFFLINE hook (no-op until the SquareLine
//                         screen gains a status widget)
//
// All functions must be called from the LVGL thread (the Arduino loop / tick).
// The generated files under src/ui/ are NEVER edited here — they are overwritten
// on every SquareLine re-export; this module is the only thing that touches them.
namespace app_ui {

void create();
void update(const Payload& p);
void set_server_status(bool online);

// Push the GPS data onto the dedicated SquareLine labels.
void set_gps_datetime(const char* text);   // UTC-3 date/time    -> ui_lbdatetime
void set_gps_altitude(const char* text);   // altitude MSL (m)   -> ui_lbaltitude
void set_gps_compass(const char* text);    // course (degrees)   -> ui_lbcompass

// Trip time (HH:MM:SS since boot) -> ui_lbtriptime.
void set_trip_time(const char* text);

// AHT20 ambient temperature (pre-formatted "XX.X °C") -> ui_lbambienttemperature.
void set_ambient_temperature(const char* text);

}  // namespace app_ui
