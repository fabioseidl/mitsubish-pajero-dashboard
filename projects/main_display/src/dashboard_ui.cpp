#include "dashboard_ui.h"

#include "lvgl/lvgl.h"
#include "dashboard_widgets.h"

namespace dashboard {

// ─────────────────────────────────────────────────────────────
// Fonts (built-in Montserrat, enabled in lv_conf.h)
// ─────────────────────────────────────────────────────────────
static const lv_font_t* FONT_TITLE  = &lv_font_montserrat_16;
static const lv_font_t* FONT_BIG     = &lv_font_montserrat_48;  // SPEED / RPM
static const lv_font_t* FONT_VALUE  = &lv_font_montserrat_28;   // info grid
static const lv_font_t* FONT_STATUS = &lv_font_montserrat_20;   // server status

static constexpr uint32_t STATUS_ONLINE_COLOR  = 0x00C853;  // green
static constexpr uint32_t STATUS_OFFLINE_COLOR = 0xFF1744;  // red

// ─────────────────────────────────────────────────────────────
// Widget handles — module-private, never exposed in the header.
// ─────────────────────────────────────────────────────────────
namespace {
struct Widgets {
    // top row
    lv_obj_t* speed = nullptr;
    lv_obj_t* rpm   = nullptr;
    // grid row 1
    lv_obj_t* fuel_rate       = nullptr;
    lv_obj_t* consumption     = nullptr;
    lv_obj_t* avg_consumption = nullptr;
    lv_obj_t* distance        = nullptr;
    // grid row 2
    lv_obj_t* baro    = nullptr;
    lv_obj_t* ambient = nullptr;
    lv_obj_t* gear    = nullptr;
    lv_obj_t* boost   = nullptr;
    // status
    lv_obj_t* status = nullptr;
};
Widgets w;
}  // namespace

using dashboard_widgets::make_flex;
using dashboard_widgets::make_stat_block;

void create() {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Root: vertical stack filling the panel, 8 px gutter between sections.
    lv_obj_t* root = make_flex(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(root, 1024, 600);
    lv_obj_set_style_pad_all(root, 8, 0);
    lv_obj_set_style_pad_row(root, 8, 0);

    // ── Top row — SPEED + RPM, large values, takes most vertical space ──
    lv_obj_t* top = make_flex(root, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(top, lv_pct(100));
    lv_obj_set_flex_grow(top, 5);
    lv_obj_set_style_pad_column(top, 8, 0);
    w.speed = make_stat_block(top, "SPEED (km/h)", FONT_TITLE, FONT_BIG, "0");
    w.rpm   = make_stat_block(top, "RPM",          FONT_TITLE, FONT_BIG, "0");

    // ── Info grid row 1 ──
    lv_obj_t* g1 = make_flex(root, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(g1, lv_pct(100));
    lv_obj_set_flex_grow(g1, 3);
    lv_obj_set_style_pad_column(g1, 8, 0);
    w.fuel_rate       = make_stat_block(g1, "FUEL RATE (L/h)",   FONT_TITLE, FONT_VALUE, "--");
    w.consumption     = make_stat_block(g1, "CONS (km/L)",       FONT_TITLE, FONT_VALUE, "--");
    w.avg_consumption = make_stat_block(g1, "AVG CONS (km/L)",   FONT_TITLE, FONT_VALUE, "--");
    w.distance        = make_stat_block(g1, "DISTANCE (km)",     FONT_TITLE, FONT_VALUE, "--");

    // ── Info grid row 2 ──
    lv_obj_t* g2 = make_flex(root, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(g2, lv_pct(100));
    lv_obj_set_flex_grow(g2, 3);
    lv_obj_set_style_pad_column(g2, 8, 0);
    w.baro    = make_stat_block(g2, "BARO (kPa)",   FONT_TITLE, FONT_VALUE, "--");
    w.ambient = make_stat_block(g2, "AMBIENT (C)",  FONT_TITLE, FONT_VALUE, "--");
    w.gear    = make_stat_block(g2, "AT GEAR",      FONT_TITLE, FONT_VALUE, "--");
    w.boost   = make_stat_block(g2, "BOOST",        FONT_TITLE, FONT_VALUE, "--");

    // ── Status bar — small fixed-height strip at the bottom ──
    lv_obj_t* status_bar = make_flex(root, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(status_bar, lv_pct(100));
    lv_obj_set_height(status_bar, 40);
    lv_obj_set_flex_align(status_bar,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    w.status = lv_label_create(status_bar);
    lv_obj_set_style_text_font(w.status, FONT_STATUS, 0);

    set_server_status(false);  // assume offline until first packet arrives
}

void update(const Payload& p) {
    if (w.speed == nullptr) return;            // create() not called yet
    if (!(p.flags & PAYLOAD_FLAG_DATA_VALID)) return;

    lv_label_set_text_fmt(w.speed,           "%u",   (unsigned)p.speed_kmh);
    lv_label_set_text_fmt(w.rpm,             "%u",   (unsigned)p.rpm);
    lv_label_set_text_fmt(w.fuel_rate,       "%.1f", p.fuel_rate_l_per_h);
    lv_label_set_text_fmt(w.consumption,     "%.1f", p.consumption_km_per_l);
    lv_label_set_text_fmt(w.avg_consumption, "%.1f", p.avg_consumption_km_per_l);
    lv_label_set_text_fmt(w.distance,        "%.1f", p.distance_km);
    lv_label_set_text_fmt(w.baro,            "%u",   (unsigned)p.baro_pressure_kpa);
    lv_label_set_text_fmt(w.ambient,         "%.0f", p.ambient_temp_c);
    lv_label_set_text_fmt(w.gear,            "%.0f", p.at_gear_pos);
    lv_label_set_text_fmt(w.boost,           "%.1f", p.boost_pres);
}

void set_server_status(bool online) {
    if (w.status == nullptr) return;

    if (online) {
        lv_label_set_text(w.status, "SERVER ONLINE");
        lv_obj_set_style_text_color(w.status, lv_color_hex(STATUS_ONLINE_COLOR), 0);
    } else {
        lv_label_set_text(w.status, "SERVER OFFLINE");
        lv_obj_set_style_text_color(w.status, lv_color_hex(STATUS_OFFLINE_COLOR), 0);
    }
}

}  // namespace dashboard
