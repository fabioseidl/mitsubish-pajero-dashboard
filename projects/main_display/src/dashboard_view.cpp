#include "dashboard_view.h"

#include <string.h>

// Assets converted by ui/convert_assets.sh; C linkage.
extern "C" {
LV_FONT_DECLARE(ui_font_roboto_bold_120);
LV_FONT_DECLARE(ui_font_roboto_bold_60);
LV_FONT_DECLARE(ui_font_roboto_regular_20);
LV_FONT_DECLARE(ui_font_roboto_regular_28);
LV_FONT_DECLARE(ui_font_roboto_regular_50);
LV_FONT_DECLARE(ui_font_mdi_24);
LV_FONT_DECLARE(ui_font_mdi_16);
}

namespace dashboard_view {
namespace {

// Fonts by role. Every label below uses one of these.
const lv_font_t* const kHero    = &ui_font_roboto_bold_120;   // speed
const lv_font_t* const kLarge   = &ui_font_roboto_bold_60;    // rpm, heading degrees
const lv_font_t* const kValue   = &ui_font_roboto_regular_50; // tile values
const lv_font_t* const kSmall   = &ui_font_roboto_regular_28; // header, bar values, gear cells
const lv_font_t* const kTitle   = &ui_font_roboto_regular_20; // panel and tile titles
const lv_font_t* const kCaption = &lv_font_montserrat_16;     // units (has U+00B0), scale numbers
const lv_font_t* const kIcon    = &ui_font_mdi_24;
const lv_font_t* const kArrow   = &ui_font_mdi_16;

// Colours used only by the view.
constexpr uint32_t kPanel     = 0x101418;   // exact in RGB565; a gradient bands with colour casts
constexpr uint32_t kLine      = 0x23272E;
constexpr uint32_t kTrack     = 0x1D2026;   // bar background, idle gear cell
constexpr uint32_t kRedline   = 0x4A1A1C;   // RPM track past 4000
constexpr uint32_t kDialFace  = 0x080C10;

// MDI glyphs (UTF-8 of the codepoints listed in ui/convert_assets.sh).
#define G_THERMO      "\U000F050F"
#define G_SNOW        "\U000F0717"
#define G_FAN         "\U000F0210"
#define G_DEFROST     "\U000F0D61"
#define G_SEAT        "\U000F0482"
#define G_RECIRC      "\U000F006A"
#define G_LEFT        "\U000F0731"
#define G_RIGHT       "\U000F0734"
#define G_ENGINE      "\U000F01FA"
#define G_LAMP_LO     "\U000F0C4A"
#define G_LAMP_HI     "\U000F0C4C"
#define G_DOOR        "\U000F0B6B"
#define G_GAUGE       "\U000F029A"
#define G_TREND       "\U000F07B1"
#define G_FUEL        "\U000F0298"
#define G_ROAD        "\U000F0462"
#define G_TIMER       "\U000F051B"
#define G_COOLANT     "\U000F03C8"
#define G_AIR         "\U000F0D43"
#define G_BATTERY     "\U000F010C"
#define G_FLAME       "\U000F0238"
#define G_MOUNTAIN    "\U000F02F5"

// Screen layout: header 0..48, panels 60..368, tile rows at 380 and 488.
constexpr int32_t kPanelY     = 60;
constexpr int32_t kPanelH     = 308;
constexpr int32_t kTileW      = 188;
constexpr int32_t kTileH      = 96;
constexpr int32_t kTilePitchX = 201;
constexpr int32_t kTileY0     = 380;
constexpr int32_t kTilePitchY = 108;

// Gear strip order and the CAN 0x218 code each cell stands for.
constexpr const char* kGearNames[kGearCells] = {"P", "R", "N", "1", "2", "3", "4", "5"};
constexpr int         kGearCodes[kGearCells] = {13, 11, 0, 1, 2, 3, 4, 5};

// Compass card: 73 ticks over 0..360 (5° apart), labelled every 6th (30°).
const char* kScaleText[] = {"N", "30", "60", "E", "120", "150", "S", "210", "240", "W", "300", "330", "", nullptr};
lv_style_t s_cardinal_style;   // E / S / W ticks and labels
lv_style_t s_north_style;      // N tick and label

/** Strips theme styling and scrolling so every style below is explicit. */
lv_obj_t* bare(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

/** A filled rectangle at (x, y). */
lv_obj_t* rect(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t hex, int32_t radius) {
    lv_obj_t* o = bare(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(hex), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(o, radius, LV_PART_MAIN);
    return o;
}

/** A panel: solid fill, 1 px border, 8 px radius; children sit inside the border. */
lv_obj_t* panel(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h) {
    lv_obj_t* o = rect(parent, x, y, w, h, kPanel, 8);
    lv_obj_set_style_border_color(o, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 1, LV_PART_MAIN);
    return o;
}

/** A label with its font and colour; unpositioned, for flex rows. */
lv_obj_t* text(lv_obj_t* parent, const lv_font_t* font, uint32_t hex, const char* txt) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_hex(hex), LV_PART_MAIN);
    lv_label_set_text(l, txt);
    return l;
}

/** A label aligned to its parent; LV_ALIGN_* keeps it anchored as its text changes width. */
lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, uint32_t hex, lv_align_t align,
                int32_t x, int32_t y, const char* txt) {
    lv_obj_t* l = text(parent, font, hex, txt);
    lv_obj_align(l, align, x, y);
    return l;
}

/** A transparent flex row of content size; cross-axis alignment as given. */
lv_obj_t* row(lv_obj_t* parent, int32_t gap, lv_flex_align_t cross) {
    lv_obj_t* r = bare(parent);
    lv_obj_set_size(r, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(r, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, cross, cross);
    lv_obj_set_style_pad_column(r, gap, LV_PART_MAIN);
    return r;
}

/** A header pill: 36 px tall, rounded ends, items centred 10 px apart. */
lv_obj_t* pill(lv_obj_t* parent) {
    lv_obj_t* p = row(parent, 10, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_height(p, 36);
    lv_obj_set_style_pad_hor(p, 12, LV_PART_MAIN);
    lv_obj_set_style_radius(p, 18, LV_PART_MAIN);
    lv_obj_set_style_border_color(p, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_border_width(p, 1, LV_PART_MAIN);
    return p;
}

/** A 1x20 divider inside a pill. */
lv_obj_t* separator(lv_obj_t* parent) {
    lv_obj_t* s = bare(parent);
    lv_obj_set_size(s, 1, 20);
    lv_obj_set_style_bg_color(s, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_MAIN);
    return s;
}

/** An lv_bar 0..max with a rounded track and indicator. */
lv_obj_t* bar(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, int32_t max, uint32_t ind_hex) {
    lv_obj_t* b = lv_bar_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_bar_set_range(b, 0, max);
    lv_obj_set_style_bg_color(b, lv_color_hex(kTrack), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(b, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, lv_color_hex(ind_hex), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(b, 4, LV_PART_INDICATOR);
    return b;
}

/** A tile: optional status strip, title, dim icon top-right, value with its unit on the baseline. */
Tile makeTile(lv_obj_t* parent, int index, const char* title, const char* unit, const char* glyph, bool strip) {
    lv_obj_t* t = panel(parent, 16 + (index % 5) * kTilePitchX, kTileY0 + (index / 5) * kTilePitchY, kTileW, kTileH);
    Tile tile{};
    if (strip) tile.strip = rect(t, 0, 14, 4, 66, color::kDim, 2);
    label(t, kTitle, color::kMuted, LV_ALIGN_TOP_LEFT, 14, 10, title);
    label(t, kIcon, color::kDim, LV_ALIGN_TOP_RIGHT, -12, 8, glyph);

    // Flex row aligned at the bottom: the unit follows the value as it widens.
    lv_obj_t* r = row(t, 6, LV_FLEX_ALIGN_END);
    lv_obj_set_pos(r, 14, 34);
    tile.value = text(r, kValue, color::kText, "0");
    lv_obj_t* u = text(r, kCaption, color::kMuted, unit);
    lv_obj_set_style_pad_bottom(u, 10, LV_PART_MAIN);   // lifts the unit onto the value's baseline
    return tile;
}

/** Header: link dot, clock, status pill, climate pill, divider. */
void buildHeader(lv_obj_t* scr, Widgets& w) {
    w.link_dot = rect(scr, 16, 18, 12, 12, color::kOffline, 6);
    w.datetime = label(scr, kSmall, color::kText, LV_ALIGN_TOP_LEFT, 38, 8, "--:--");

    // Status pill: turn signals bracket engine fault, head lamps, doors.
    lv_obj_t* st = pill(scr);
    lv_obj_set_pos(st, 130, 6);
    w.turn_l = text(st, kIcon, color::kDim, G_LEFT);
    separator(st);
    w.engine = text(st, kIcon, color::kDim, G_ENGINE);
    w.dtc    = text(st, kSmall, color::kWarn, "0");
    lv_obj_add_flag(w.dtc, LV_OBJ_FLAG_HIDDEN);
    w.lamp   = text(st, kIcon, color::kDim, G_LAMP_LO);
    w.door   = text(st, kIcon, color::kDim, G_DOOR);
    separator(st);
    w.turn_r = text(st, kIcon, color::kDim, G_RIGHT);

    // Climate pill: cabin temperature | A/C, set point, fan, defrost, vent mode, recirculation.
    lv_obj_t* cl = pill(scr);
    lv_obj_align(cl, LV_ALIGN_TOP_RIGHT, -16, 6);
    text(cl, kIcon, color::kMuted, G_THERMO);
    w.cabin_temp = text(cl, kSmall, color::kText, "--");
    separator(cl);
    w.ac       = text(cl, kIcon, color::kDim, G_SNOW);
    w.set_temp = text(cl, kSmall, color::kDim, "--");
    w.fan      = text(cl, kIcon, color::kDim, G_FAN);

    // Five bars rising 6..18 px, bottom-aligned.
    lv_obj_t* bars = row(cl, 2, LV_FLEX_ALIGN_END);
    lv_obj_set_height(bars, 18);
    for (int i = 0; i < kFanBars; i++) {
        w.fan_bars[i] = bare(bars);
        lv_obj_set_size(w.fan_bars[i], 4, 6 + i * 3);
        lv_obj_set_style_radius(w.fan_bars[i], 1, LV_PART_MAIN);
        lv_obj_set_style_bg_color(w.fan_bars[i], lv_color_hex(color::kDim), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(w.fan_bars[i], LV_OPA_COVER, LV_PART_MAIN);
    }
    w.defrost = text(cl, kIcon, color::kDim, G_DEFROST);

    // Vent mode: seated figure facing right, arrows from the dash at face / feet height.
    lv_obj_t* vent = bare(cl);
    lv_obj_set_size(vent, 40, 24);
    lv_obj_add_flag(vent, LV_OBJ_FLAG_OVERFLOW_VISIBLE);   // the face arrow rises above the box
    w.seat      = label(vent, kIcon, color::kDim, LV_ALIGN_TOP_LEFT, 0, 0, G_SEAT);
    w.vent_face = label(vent, kArrow, color::kCold, LV_ALIGN_TOP_LEFT, 22, -1, G_LEFT);
    w.vent_feet = label(vent, kArrow, color::kCold, LV_ALIGN_TOP_LEFT, 25, 13, G_LEFT);
    lv_obj_add_flag(w.vent_face, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(w.vent_feet, LV_OBJ_FLAG_HIDDEN);
    w.recirc = text(cl, kIcon, color::kDim, G_RECIRC);

    rect(scr, 0, 48, 1024, 1, kLine, 0);
}

/** Engine panel: RPM with redline track, boost / load / throttle bars. */
void buildEngine(lv_obj_t* scr, Widgets& w) {
    lv_obj_t* p = panel(scr, 16, kPanelY, 300, kPanelH);
    label(p, kTitle, color::kMuted, LV_ALIGN_TOP_LEFT, 16, 14, "RPM");
    w.rpm = label(p, kLarge, color::kText, LV_ALIGN_TOP_LEFT, 16, 34, "0");

    // Redline 4000..4500 tinted into the track: the track and its red end are rects under a
    // bar with a transparent main part, so the bar itself stays a plain fill.
    rect(p, 16, 106, 266, 18, kTrack, 4);
    rect(p, 16 + 266 * 4000 / 4500, 106, 266 - 266 * 4000 / 4500, 18, kRedline, 4);
    w.rpm_bar = bar(p, 16, 106, 266, 18, 4500, color::kOnline);
    lv_obj_set_style_bg_opa(w.rpm_bar, LV_OPA_TRANSP, LV_PART_MAIN);

    label(p, kTitle, color::kMuted, LV_ALIGN_TOP_LEFT, 16, 154, "BOOST");
    w.boost     = label(p, kSmall, color::kText, LV_ALIGN_TOP_RIGHT, -16, 148, "0.00 bar");
    w.boost_bar = bar(p, 16, 182, 266, 8, 200, color::kCold);   // centibar

    label(p, kTitle, color::kMuted, LV_ALIGN_TOP_LEFT, 16, 208, "LOAD");
    w.load     = label(p, kSmall, color::kText, LV_ALIGN_TOP_RIGHT, -16, 202, "0 %");
    w.load_bar = bar(p, 16, 236, 266, 8, 100, color::kMuted);

    label(p, kTitle, color::kMuted, LV_ALIGN_TOP_LEFT, 16, 262, "THROTTLE");
    w.throttle     = label(p, kSmall, color::kText, LV_ALIGN_TOP_RIGHT, -16, 256, "0 %");
    w.throttle_bar = bar(p, 16, 290, 266, 8, 100, color::kMuted);
}

/** Speed panel: hero speed and the P R N 1-5 gear strip. */
void buildSpeed(lv_obj_t* scr, Widgets& w) {
    lv_obj_t* p = panel(scr, 328, kPanelY, 368, kPanelH);
    w.speed = label(p, kHero, color::kText, LV_ALIGN_TOP_MID, 0, 40, "0");
    label(p, kTitle, color::kMuted, LV_ALIGN_TOP_MID, 0, 168, "km/h");
    label(p, kTitle, color::kMuted, LV_ALIGN_TOP_LEFT, 18, 212, "GEAR");

    for (int i = 0; i < kGearCells; i++) {
        lv_obj_t* cell = rect(p, 18 + i * 42, 240, 36, 48, kTrack, 8);
        lv_obj_set_style_border_width(cell, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(cell, lv_color_hex(kTrack), LV_PART_MAIN);
        label(cell, kSmall, color::kDim, LV_ALIGN_CENTER, 0, -1, kGearNames[i]);
        w.gear_cells[i] = cell;
    }
}

/** Heading panel: lv_scale compass card on a dark disk, fixed lubber mark, degrees + cardinal. */
void buildHeading(lv_obj_t* scr, Widgets& w) {
    lv_obj_t* p = panel(scr, 708, kPanelY, 300, kPanelH);
    label(p, kTitle, color::kMuted, LV_ALIGN_TOP_LEFT, 16, 14, "HEADING");

    lv_obj_t* dial = rect(p, 31, 58, 236, 236, kDialFace, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_color(dial, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_border_width(dial, 1, LV_PART_MAIN);

    // Card inset 2 px inside the disk border: ticks run inward from r = 116.
    lv_obj_t* s = lv_scale_create(p);
    lv_obj_remove_style_all(s);
    lv_obj_set_pos(s, 33, 60);
    lv_obj_set_size(s, 232, 232);
    lv_scale_set_mode(s, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_range(s, 0, 360);
    lv_scale_set_angle_range(s, 360);
    lv_scale_set_total_tick_count(s, 73);
    lv_scale_set_major_tick_every(s, 6);
    lv_scale_set_label_show(s, true);
    lv_scale_set_text_src(s, kScaleText);
    lv_obj_set_style_arc_width(s, 0, LV_PART_MAIN);            // no main arc, ticks only

    // Majors (30°): 10 px, muted, numbers at r = 116 - 10 - (7 + 15) = 84.
    lv_obj_set_style_length(s, 10, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(s, 2, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(s, lv_color_hex(color::kMuted), LV_PART_INDICATOR);
    lv_obj_set_style_line_rounded(s, true, LV_PART_INDICATOR);
    lv_obj_set_style_text_font(s, kCaption, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(s, lv_color_hex(color::kMuted), LV_PART_INDICATOR);
    lv_obj_set_style_pad_radial(s, 7, LV_PART_INDICATOR);
    // Minors (5°): 5 px, dim.
    lv_obj_set_style_length(s, 5, LV_PART_ITEMS);
    lv_obj_set_style_line_width(s, 1, LV_PART_ITEMS);
    lv_obj_set_style_line_color(s, lv_color_hex(color::kDim), LV_PART_ITEMS);

    // Cardinals: heavier tick and the 28 px font; north in red at both ends of the range.
    lv_style_init(&s_cardinal_style);
    lv_style_set_line_width(&s_cardinal_style, 3);
    lv_style_set_line_color(&s_cardinal_style, lv_color_hex(color::kText));
    lv_style_set_text_color(&s_cardinal_style, lv_color_hex(color::kText));
    lv_style_set_text_font(&s_cardinal_style, kSmall);
    lv_style_init(&s_north_style);
    lv_style_set_line_width(&s_north_style, 3);
    lv_style_set_line_color(&s_north_style, lv_color_hex(color::kOffline));
    lv_style_set_text_color(&s_north_style, lv_color_hex(color::kOffline));
    lv_style_set_text_font(&s_north_style, kSmall);
    for (int32_t deg = 0; deg <= 360; deg += 90) {
        lv_scale_section_t* sec = lv_scale_add_section(s);
        lv_scale_set_section_range(s, sec, deg, deg);
        lv_scale_set_section_style_indicator(s, sec, (deg % 360 == 0) ? &s_north_style : &s_cardinal_style);
    }
    w.compass_card = s;

    w.lubber   = rect(p, 147, 50, 4, 18, color::kWarn, 2);
    w.heading  = label(p, kLarge, color::kText, LV_ALIGN_TOP_MID, 0, 140, "--");
    w.cardinal = label(p, kSmall, color::kMuted, LV_ALIGN_TOP_MID, 0, 203, "NO FIX");
}

}  // namespace

Widgets build(lv_obj_t* scr) {
    Widgets w{};

    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    buildHeader(scr, w);
    buildEngine(scr, w);
    buildSpeed(scr, w);
    buildHeading(scr, w);

    w.consumption     = makeTile(scr, 0, "NOW",        "km/L", G_GAUGE,    false);
    w.avg_consumption = makeTile(scr, 1, "AVERAGE",    "km/L", G_TREND,    false);
    w.fuel_rate       = makeTile(scr, 2, "FUEL RATE",  "L/h",  G_FUEL,     false);
    w.distance        = makeTile(scr, 3, "TRIP",       "km",   G_ROAD,     false);
    w.trip_time       = makeTile(scr, 4, "TRIP TIME",  "h",    G_TIMER,    false);
    w.coolant         = makeTile(scr, 5, "COOLANT",    "\xC2\xB0" "C", G_COOLANT, true);
    w.iat             = makeTile(scr, 6, "INTAKE AIR", "\xC2\xB0" "C", G_AIR,     false);
    w.voltage         = makeTile(scr, 7, "BATTERY",    "V",    G_BATTERY,  true);
    w.catalyst        = makeTile(scr, 8, "CATALYST",   "\xC2\xB0" "C", G_FLAME,   false);
    w.altitude        = makeTile(scr, 9, "ALTITUDE",   "m",    G_MOUNTAIN, false);

    setHeading(w, -1);
    return w;
}

void setLit(lv_obj_t* obj, bool on, uint32_t on_color) {
    const lv_color_t c = lv_color_hex(on ? on_color : color::kDim);
    if (!lv_color_eq(lv_obj_get_style_text_color(obj, LV_PART_MAIN), c)) {
        lv_obj_set_style_text_color(obj, c, LV_PART_MAIN);
    }
}

void setBgColor(lv_obj_t* obj, uint32_t hex) {
    const lv_color_t c = lv_color_hex(hex);
    if (!lv_color_eq(lv_obj_get_style_bg_color(obj, LV_PART_MAIN), c)) {
        lv_obj_set_style_bg_color(obj, c, LV_PART_MAIN);
    }
}

void setHidden(lv_obj_t* obj, bool hidden) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == hidden) return;
    if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void setGear(Widgets& w, int gear, int target) {
    for (int i = 0; i < kGearCells; i++) {
        lv_obj_t* cell = w.gear_cells[i];
        lv_obj_t* name = lv_obj_get_child(cell, 0);
        const bool cur = kGearCodes[i] == gear;
        const bool tgt = !cur && kGearCodes[i] == target;
        setBgColor(cell, cur ? color::kText : kTrack);
        const lv_color_t border = lv_color_hex(cur ? color::kText : tgt ? color::kWarn : kTrack);
        if (!lv_color_eq(lv_obj_get_style_border_color(cell, LV_PART_MAIN), border)) {
            lv_obj_set_style_border_color(cell, border, LV_PART_MAIN);
        }
        const lv_color_t ink = lv_color_hex(cur ? 0x000000 : tgt ? color::kWarn : color::kDim);
        if (!lv_color_eq(lv_obj_get_style_text_color(name, LV_PART_MAIN), ink)) {
            lv_obj_set_style_text_color(name, ink, LV_PART_MAIN);
        }
    }
}

void setLamps(Widgets& w, bool low, bool high) {
    const char* glyph = high ? G_LAMP_HI : G_LAMP_LO;
    if (strcmp(lv_label_get_text(w.lamp), glyph) != 0) lv_label_set_text(w.lamp, glyph);
    setLit(w.lamp, low || high, high ? color::kCold : color::kOnline);
}

void setFanBars(Widgets& w, int lit) {
    for (int i = 0; i < kFanBars; i++) setBgColor(w.fan_bars[i], i < lit ? color::kCold : color::kDim);
}

void setHeading(Widgets& w, int deg) {
    const bool fix = deg >= 0;
    // lv_scale angles run clockwise from 3 o'clock; 270 puts the card's 0 (N) at the top.
    lv_scale_set_rotation(w.compass_card, 270 - (fix ? deg : 0));
    // Fade via line / text opa: section styles fall back to these, but not to the object's opa.
    const lv_opa_t opa = fix ? LV_OPA_COVER : LV_OPA_30;
    lv_obj_set_style_line_opa(w.compass_card, opa, LV_PART_INDICATOR);
    lv_obj_set_style_line_opa(w.compass_card, opa, LV_PART_ITEMS);
    lv_obj_set_style_text_opa(w.compass_card, opa, LV_PART_INDICATOR);
    setHidden(w.lubber, !fix);
}

}  // namespace dashboard_view
