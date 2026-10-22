#pragma once

#include <lvgl.h>

/**
 * The dashboard's LVGL widget tree for the 1024x600 panel — pure LVGL, no board
 * code, so it builds for the firmware and for the native simulator (sim/).
 *
 * Only builds widgets; app_ui formats the data and writes the labels.
 */
namespace dashboard_view {

/** Handles to every label app_ui writes. Static titles are not exposed. */
struct Widgets {
    lv_obj_t* server_status;     // "." dot, recoloured online/offline
    lv_obj_t* datetime;          // GPS "HH:MM"
    lv_obj_t* cabin_temp;        // AHT20 "XX.X C"

    lv_obj_t* speed;
    lv_obj_t* compass_cardinal;  // "NNE"
    lv_obj_t* compass;           // course degrees

    lv_obj_t* rpm;
    lv_obj_t* gear;              // current AT gear, large
    lv_obj_t* target_gear;       // AT target gear, small

    lv_obj_t* altitude;
    lv_obj_t* humidity;
    lv_obj_t* acceleration;      // |accel|
    lv_obj_t* axis_x;
    lv_obj_t* axis_y;
    lv_obj_t* axis_z;

    lv_obj_t* avg_consumption;
    lv_obj_t* consumption;       // instant, small, beside the average
    lv_obj_t* fuel_rate;
    lv_obj_t* boost;
    lv_obj_t* engine_load;

    lv_obj_t* distance;
    lv_obj_t* trip_time;
    lv_obj_t* voltage;
    lv_obj_t* coolant;
    lv_obj_t* coolant_status;    // "." dot, recoloured by temperature bucket
};

/** Builds the dashboard on scr and returns the label handles. Call once, on the LVGL thread. */
Widgets build(lv_obj_t* scr);

}  // namespace dashboard_view
