#include "dashboard_view.h"

// Assets converted from ui/main_display/assets (lv_font_conv / LVGL image converter); C linkage.
extern "C" {
LV_FONT_DECLARE(ui_font_robotobold120);
LV_FONT_DECLARE(ui_font_robotobold60);
LV_FONT_DECLARE(ui_font_robotoregular20);
LV_FONT_DECLARE(ui_font_robotoregular28);
LV_FONT_DECLARE(ui_font_robotoregular50);
LV_IMAGE_DECLARE(ui_img_background_png);
LV_IMAGE_DECLARE(ui_img_icon_temperature_png);
}

namespace dashboard_view {
namespace {

// Fonts by role. Every widget below uses one of these.
const lv_font_t* const kHero    = &ui_font_robotobold120;   // speed, gear, status dots
const lv_font_t* const kLarge   = &ui_font_robotobold60;    // rpm, compass cardinal
const lv_font_t* const kValue   = &ui_font_robotoregular50; // tile values
const lv_font_t* const kSmall   = &ui_font_robotoregular28; // secondary values, header
const lv_font_t* const kTitle   = &ui_font_robotoregular20; // tile titles
const lv_font_t* const kCaption = &lv_font_montserrat_16;   // "AXIS"

/**
 * Creates a label centred on (512 + dx, 300 + dy). LV_ALIGN_CENTER keeps the text
 * centred on that point as its width changes, so "9" and "120" share a centre.
 */
lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, int32_t dx, int32_t dy, const char* text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_align(l, LV_ALIGN_CENTER, dx, dy);
    return l;
}

/** Creates an image centred on (512 + dx, 300 + dy). */
lv_obj_t* image(lv_obj_t* parent, const void* src, int32_t dx, int32_t dy) {
    lv_obj_t* img = lv_image_create(parent);
    lv_image_set_src(img, src);
    lv_obj_align(img, LV_ALIGN_CENTER, dx, dy);
    return img;
}

}  // namespace

Widgets build(lv_obj_t* scr) {
    Widgets w{};

    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    // Tile frames, dividers, tick scales and the logo are all in the background image.
    image(scr, &ui_img_background_png, 0, 0);

    // Creation order is z-order (the large "." dots overlap their neighbours' boxes),
    // so widgets are listed in stacking order, not reading order.

    w.server_status   = label(scr, kHero,  482, -303, ".");

    label(scr, kTitle,  135, 110, "BOOST bar");
    w.boost           = label(scr, kValue, 128, 159, "0");
    label(scr, kTitle,  385, 110, "LOAD %");
    w.engine_load     = label(scr, kValue, 381, 159, "0");
    label(scr, kTitle, -130, 110, "FUEL L/h");
    w.fuel_rate       = label(scr, kValue, -130, 158, "0");
    w.consumption     = label(scr, kSmall, -297, 164, "0");
    label(scr, kTitle, -375, 110, "AVG km/L");
    w.avg_consumption = label(scr, kValue, -380, 159, "0");
    label(scr, kTitle, -375, 213, "TRIP km");
    w.distance        = label(scr, kValue, -380, 260, "0");
    label(scr, kTitle,  385, 215, "COOLANT C");
    w.coolant         = label(scr, kValue,  376, 260, "0");

    w.compass         = label(scr, kSmall, -146, -66, "0");

    w.altitude        = label(scr, kValue, -380,  56, "0");
    w.voltage         = label(scr, kValue,  132, 261, "0");

    w.gear            = label(scr, kHero,   461, -141, "-");
    w.target_gear     = label(scr, kValue,  473,  -59, "-");
    w.rpm             = label(scr, kLarge,  253, -110, "0");
    label(scr, kTitle,   42, -178, "RPM");

    w.datetime        = label(scr, kSmall,  392, -273, "00:00");
    label(scr, kTitle, -130, 215, "TRIP Time");
    w.trip_time       = label(scr, kValue, -130, 262, "0");
    label(scr, kTitle, -333, -272, "PAJERO DAKAR 3.2 DI-D");
    label(scr, kTitle,  135, 215, "BATTERY");
    w.cabin_temp      = label(scr, kSmall,  278, -273, "0 C");
    image(scr, &ui_img_icon_temperature_png, 213, -276);

    w.compass_cardinal = label(scr, kLarge, -147, -124, "N");
    w.coolant_status   = label(scr, kHero,   307,  229, ".");

    w.humidity        = label(scr, kValue, -130, 56, "0");
    w.acceleration    = label(scr, kValue,  132, 56, "0");
    w.axis_x          = label(scr, kSmall,  346, 55, "0");
    w.axis_y          = label(scr, kSmall,  403, 55, "0");
    w.axis_z          = label(scr, kSmall,  462, 55, "0");

    label(scr, kTitle,  -375, 10, "ALTITUDE");
    label(scr, kTitle,  -130, 10, "HUMIDITY %");
    label(scr, kTitle,   135, 10, "ACCELERATION");
    label(scr, kTitle,   385, 10, "ROTATION");
    label(scr, kCaption, 289, 55, "AXIS");

    w.speed           = label(scr, kHero, -360, -114, "0");
    return w;
}

}  // namespace dashboard_view
