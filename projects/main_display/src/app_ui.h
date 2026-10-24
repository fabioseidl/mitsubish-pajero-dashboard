#pragma once

#include "payload.h"

// Bridge between the app and the dashboard view (dashboard_view.h). Formats the
// data and writes it to the view's widgets, so main.cpp never touches widgets:
//   - create()            builds the dashboard (after lv_init() and display
//                         registration) plus a backlight readout (header centre)
//                         driving the 10-step StepBrightness cycle. This board's
//                         backlight is on/off only (PCF8574 P1), so levels are the
//                         opacity of a black overlay on lv_layer_top().
//   - update()            pushes a received Payload into the panels, tiles and pills
//   - set_server_status() recolours the link dot: green online, red offline
//
// All functions must be called from the LVGL thread (the Arduino loop / tick).
namespace app_ui {

void create();
void update(const Payload& p);
void set_server_status(bool online);

// Advance the backlight one step (10 → 20 → … → 100 → 10) and update the label.
// Called by the on-screen button (inert: this board's GT911 does not answer).
void cycle_backlight();

// Step the backlight one level up or down, clamped at 10% / 100%, and show the
// readout. Called by the physical UP/DOWN buttons on GPIO 6.
void step_backlight(bool up);

// GPS strings from gps.h.
void set_gps_datetime(const char* text);   // "YYYY-MM-DD HH:MM:SS" (UTC-3), shown as HH:MM
void set_gps_altitude(const char* text);   // altitude MSL (m)
void set_gps_compass(const char* text);    // course (degrees) or "--"; turns the compass card

// AHT20 cabin temperature, pre-formatted "XX.X C" (the font has no ° glyph).
void set_ambient_temperature(const char* text);

}  // namespace app_ui
