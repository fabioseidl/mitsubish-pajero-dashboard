#include "hud_ui.h"

// Roboto Bold, generated from ui/client_simple_hud/assets/Roboto-Bold.ttf by
// lv_font_conv (see CLAUDE.md). The 260 px face carries digits and '-' only, so
// its line height (191 px) is the digit box itself — no ascent padding above.
// Three digits advance ~447 px, inside the 480 px landscape screen.
extern const lv_font_t ui_font_roboto_bold_260;
extern const lv_font_t ui_font_roboto_bold_28;

static constexpr int16_t BTN_MARGIN = 8;
static constexpr int16_t BTN_W      = 74;
static constexpr int16_t BTN_H      = 62;

// Gap above the digits. The face has no ascent padding, so this is the whole
// visual margin — drop it to 0 to sit the number flush against the top edge.
static constexpr int16_t SPEED_TOP_MARGIN = 16;

/** Creates one dark rounded button with a centred text label. */
static lv_obj_t* makeButton(lv_obj_t* parent, const char* text) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, BTN_W, BTN_H);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x606060), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &ui_font_roboto_bold_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0xC0C0C0), LV_PART_MAIN);
    lv_obj_center(label);
    return btn;
}

void HudUi::build(lv_obj_t* scr, Callback on_up, Callback on_down, void* ctx) {
    on_up_   = on_up;
    on_down_ = on_down;
    ctx_     = ctx;

    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    // Speed — the point of the whole device. Sits at the top; the brightness row
    // owns the bottom, so the two never overlap and the number never shifts.
    speed_label_ = lv_label_create(scr);
    lv_obj_set_style_text_font(speed_label_, &ui_font_roboto_bold_260, LV_PART_MAIN);

    // Brightness row along the bottom edge: '-' left, current percent centred,
    // '+' right. The buttons take opposite corners so a blind tap on a moving
    // vehicle cannot hit the wrong one.
    bright_down_btn_ = makeButton(scr, "-");
    bright_up_btn_   = makeButton(scr, "+");
    lv_obj_align(bright_down_btn_, LV_ALIGN_BOTTOM_LEFT,   BTN_MARGIN, -BTN_MARGIN);
    lv_obj_align(bright_up_btn_,   LV_ALIGN_BOTTOM_RIGHT, -BTN_MARGIN, -BTN_MARGIN);
    lv_obj_add_event_cb(bright_down_btn_, onDownClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(bright_up_btn_,   onUpClicked,   LV_EVENT_CLICKED, this);

    bright_pct_label_ = lv_label_create(scr);
    lv_obj_set_style_text_font(bright_pct_label_, &ui_font_roboto_bold_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(bright_pct_label_, lv_color_hex(0xC0C0C0), LV_PART_MAIN);

    setSpeed(0, false);  // "--" offline placeholder until the first payload
}

void HudUi::setSpeed(uint8_t kmh, bool online) {
    if (online) {
        lv_label_set_text_fmt(speed_label_, "%u", kmh);
        lv_obj_set_style_text_color(speed_label_, lv_color_white(), LV_PART_MAIN);
    } else {
        // No server: dashes in grey, so a stale reading is never mistaken for live.
        lv_label_set_text(speed_label_, "--");
        lv_obj_set_style_text_color(speed_label_, lv_color_hex(0x505050), LV_PART_MAIN);
    }
    // Re-align after every text change: the label auto-sizes, so "9" and "120"
    // would otherwise sit at different offsets.
    lv_obj_align(speed_label_, LV_ALIGN_TOP_MID, 0, SPEED_TOP_MARGIN);
}

void HudUi::setBrightnessPercent(uint8_t percent) {
    lv_label_set_text_fmt(bright_pct_label_, "%u%%", percent);

    // Centre the label on the button row: LVGL anchors a BOTTOM_MID object by its
    // bottom edge, so lift it by half its own height to line the text's middle up
    // with the buttons' middle. Re-applied on every change — the label auto-sizes,
    // and LVGL does not re-run the alignment itself, so "10%" and "100%" would
    // otherwise drift sideways.
    const int16_t half_text = (int16_t)(lv_font_get_line_height(&ui_font_roboto_bold_28) / 2);
    lv_obj_align(bright_pct_label_, LV_ALIGN_BOTTOM_MID, 0,
                 half_text - (BTN_MARGIN + BTN_H / 2));
}

void HudUi::setButtonsVisible(bool visible) {
    if (buttons_visible_ == visible) return;
    buttons_visible_ = visible;

    // LVGL invalidates the area itself, so the speed underneath repaints.
    lv_obj_t* row[] = {bright_up_btn_, bright_down_btn_, bright_pct_label_};
    for (lv_obj_t* obj : row) {
        if (visible) lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

void HudUi::onUpClicked(lv_event_t* e) {
    HudUi* self = static_cast<HudUi*>(lv_event_get_user_data(e));
    if (self->on_up_) self->on_up_(self->ctx_);
}

void HudUi::onDownClicked(lv_event_t* e) {
    HudUi* self = static_cast<HudUi*>(lv_event_get_user_data(e));
    if (self->on_down_) self->on_down_(self->ctx_);
}
