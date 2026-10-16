#include "simple_hud_ui.h"

#include <stdio.h>

// Assets converted by ui/convert_assets.sh; C linkage.
extern "C" {
LV_FONT_DECLARE(ui_font_roboto_bold_150);
LV_FONT_DECLARE(ui_font_roboto_regular_24);
LV_IMAGE_DECLARE(ui_img_mitsubishi_logo);
LV_IMAGE_DECLARE(ui_img_icon_server);
}

namespace {

constexpr int32_t LOGO_Y        = -20;  // logo centre above the screen centre, clear of the caption
constexpr int32_t BOTTOM_MARGIN = 10;   // boot caption and rpm sit this far above the bottom edge
constexpr int32_t ICON_MARGIN   = 5;    // server icon inset from the top-right corner

/** Creates a non-scrolling screen. */
lv_obj_t* makeScreen() {
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

/** Creates a label in the given font, aligned to the parent. */
lv_obj_t* makeLabel(lv_obj_t* parent, const lv_font_t* font, lv_align_t align, int32_t dx, int32_t dy,
                    const char* text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_align(l, align, dx, dy);
    return l;
}

/** Creates an image aligned to the parent. */
lv_obj_t* makeImage(lv_obj_t* parent, const void* src, lv_align_t align, int32_t dx, int32_t dy) {
    lv_obj_t* img = lv_image_create(parent);
    lv_image_set_src(img, src);
    lv_obj_align(img, align, dx, dy);
    return img;
}

}  // namespace

void SimpleHudUi::build() {
    boot_ = makeScreen();
    makeImage(boot_, &ui_img_mitsubishi_logo, LV_ALIGN_CENTER, 0, LOGO_Y);
    makeLabel(boot_, &ui_font_roboto_regular_24, LV_ALIGN_BOTTOM_MID, 0, -BOTTOM_MARGIN, "PAJERO DAKAR");

    hud_ = makeScreen();
    speed_ = makeLabel(hud_, &ui_font_roboto_bold_150, LV_ALIGN_CENTER, 0, 0, "0");
    lv_obj_set_style_text_color(speed_, lv_color_white(), LV_PART_MAIN);
    rpm_ = makeLabel(hud_, &ui_font_roboto_regular_24, LV_ALIGN_BOTTOM_MID, 0, -BOTTOM_MARGIN, "0");
    server_icon_ = makeImage(hud_, &ui_img_icon_server, LV_ALIGN_TOP_RIGHT, -ICON_MARGIN, ICON_MARGIN);

    lv_screen_load(boot_);
    lv_screen_load_anim(hud_, LV_SCR_LOAD_ANIM_FADE_ON, BOOT_FADE_MS, BOOT_HOLD_MS, false);
}

void SimpleHudUi::setSpeed(uint8_t kmh) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%u", (unsigned)kmh);
    lv_label_set_text(speed_, buf);
}

void SimpleHudUi::setRpm(uint16_t rpm) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%u", (unsigned)rpm);
    lv_label_set_text(rpm_, buf);
}

void SimpleHudUi::setServerOnline(bool online) {
    if (online) {
        lv_obj_add_flag(server_icon_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(server_icon_, LV_OBJ_FLAG_HIDDEN);
    }
}
