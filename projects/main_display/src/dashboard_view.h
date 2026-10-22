#pragma once

#include <lvgl.h>

/**
 * The dashboard's LVGL widget tree for the 1024x600 panel — pure LVGL, no board
 * code, so it builds for the firmware and for the native simulator (sim/).
 * sim/prototype.html is the design it is built from.
 *
 * build() creates the widgets; the set*() helpers own the styling of state that is
 * more than a text change (lit icons, bars, the gear strip, the compass card).
 * app_ui decides what to show and formats the text.
 */
namespace dashboard_view {

/** Palette shared with app_ui, as lv_color_hex() takes it. */
namespace color {
constexpr uint32_t kText    = 0xFAFAFA;
constexpr uint32_t kMuted   = 0x8A9099;   // titles and units
constexpr uint32_t kDim     = 0x3A3F47;   // unlit icons, idle gear cells
constexpr uint32_t kOnline  = 0x29CC5B;
constexpr uint32_t kOffline = 0xE03B3B;
constexpr uint32_t kCold    = 0x3B8EE0;
constexpr uint32_t kWarn    = 0xF0A020;
}  // namespace color

/** One bottom-row tile: the value label, its unit, and the optional status strip. */
struct Tile {
    lv_obj_t* value;
    lv_obj_t* strip;   // nullptr on tiles without one
};

constexpr int kGearCells = 8;   // P R N 1 2 3 4 5
constexpr int kFanBars   = 5;

/** Handles to everything app_ui writes. Static titles and frames are not exposed. */
struct Widgets {
    // Header
    lv_obj_t* link_dot;
    lv_obj_t* datetime;
    lv_obj_t* turn_l;
    lv_obj_t* turn_r;
    lv_obj_t* engine;              // fault icon
    lv_obj_t* dtc;                 // fault count, hidden at 0
    lv_obj_t* lamp;                // dipped / main-beam glyph
    lv_obj_t* door;
    lv_obj_t* cabin_temp;
    lv_obj_t* ac;
    lv_obj_t* set_temp;
    lv_obj_t* fan;
    lv_obj_t* fan_bars[kFanBars];
    lv_obj_t* defrost;
    lv_obj_t* seat;
    lv_obj_t* vent_face;
    lv_obj_t* vent_feet;
    lv_obj_t* recirc;

    // Engine panel
    lv_obj_t* rpm;
    lv_obj_t* rpm_bar;
    lv_obj_t* boost;
    lv_obj_t* boost_bar;
    lv_obj_t* load;
    lv_obj_t* load_bar;
    lv_obj_t* throttle;
    lv_obj_t* throttle_bar;

    // Speed panel
    lv_obj_t* speed;
    lv_obj_t* gear_cells[kGearCells];

    // Heading panel
    lv_obj_t* compass_card;        // lv_scale, rotated heading-up
    lv_obj_t* lubber;
    lv_obj_t* heading;             // "047"
    lv_obj_t* cardinal;            // "NE"

    // Tiles
    Tile consumption, avg_consumption, fuel_rate, distance, trip_time;
    Tile coolant, iat, voltage, catalyst, altitude;
};

/** Builds the dashboard on scr and returns the handles. Call once, on the LVGL thread. */
Widgets build(lv_obj_t* scr);

/** Colours a label or icon `on_color` when on, kDim when off. No-op when unchanged. */
void setLit(lv_obj_t* obj, bool on, uint32_t on_color = color::kText);

/** Sets an object's background colour. No-op when unchanged. */
void setBgColor(lv_obj_t* obj, uint32_t hex);

/** Shows or hides an object. No-op when unchanged. */
void setHidden(lv_obj_t* obj, bool hidden);

/** Fills the current gear cell and outlines the target; codes are CAN 0x218 (0=N, 1-5, 11=R, 13=P). */
void setGear(Widgets& w, int gear, int target);

/** Head-lamp icon: dipped glyph green, main-beam glyph blue, dim when off. */
void setLamps(Widgets& w, bool low, bool high);

/** Lights the first `lit` fan bars. */
void setFanBars(Widgets& w, int lit);

/** Turns the compass card heading-up to `deg`; a negative value means no fix (card faded, north up). */
void setHeading(Widgets& w, int deg);

}  // namespace dashboard_view
