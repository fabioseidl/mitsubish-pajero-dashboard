#pragma once

#include "payload.h"

// Bridge between the app and the SquareLine Studio generated interface in
// src/ui/ (an LVGL 9 export). Same three-function contract the old dashboard_ui
// exposed, so main.cpp stays decoupled from the widget tree:
//   - create()            builds the screen and loads it (call once, after
//                         lv_init() and the LVGL display is registered), and
//                         builds a backlight button (top-left) driving a
//                         10-step brightness cycle
//                         (10,20,…,100%, wrapping) driven by StepBrightness
//                         from lib/core — the same cycle main_hud uses.
//                         NOTE: this board's backlight has no PWM (PCF8574 P1
//                         is on/off only), so the levels are realised as the
//                         opacity of a black overlay on lv_layer_top(), not as
//                         backlight power. See app_ui.cpp for why.
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

// Advance the backlight one step (10 → 20 → … → 100 → 10) and update the label.
// Called by the on-screen button and by the physical button on GPIO 6 — the
// latter is what actually works today, since this board's GT911 does not answer
// on the I2C bus. Must be called from the LVGL thread.
void cycle_backlight();

// Push the GPS data onto the dedicated SquareLine labels.
void set_gps_datetime(const char* text);   // UTC-3 date/time    -> ui_lbdatetime
void set_gps_altitude(const char* text);   // altitude MSL (m)   -> ui_lbaltitude
void set_gps_compass(const char* text);    // course (degrees)   -> ui_lbcompass

// Trip time (HH:MM:SS since boot) -> ui_lbtriptime.
void set_trip_time(const char* text);

// AHT20 ambient temperature (pre-formatted "XX.X °C") -> ui_lbambienttemperature.
void set_ambient_temperature(const char* text);

// AHT20 relative humidity (pre-formatted, e.g. "62 %") -> ui_lbhumidity.
void set_humidity(const char* text);

// MPU6050/6500 IMU readings -> ui_lbxaxis/yaxis/zaxis (accel, m/s^2),
// ui_lbgyroxaxis/yaxis/zaxis (gyro, rad/s) and ui_lbaceleration (|accel| m/s^2).
void set_imu(float ax, float ay, float az, float gx, float gy, float gz);

}  // namespace app_ui
