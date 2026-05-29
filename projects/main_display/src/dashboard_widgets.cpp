#include "dashboard_widgets.h"

namespace dashboard_widgets {

// Card border color — subtle dark grey so blocks read as separate cells on a
// black background without adding visual noise.
static constexpr uint32_t CARD_BORDER  = 0x2A2A2A;
static constexpr uint32_t TITLE_COLOR  = 0x9E9E9E;  // dim grey for the label
static constexpr uint32_t VALUE_COLOR  = 0xFFFFFF;  // white for the value

lv_obj_t* make_flex(lv_obj_t* parent, lv_flex_flow_t flow) {
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);              // transparent, no border, no padding
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(c, flow);
    return c;
}

lv_obj_t* make_stat_block(lv_obj_t* parent,
                          const char* title,
                          const lv_font_t* title_font,
                          const lv_font_t* value_font,
                          const char* initial_value) {
    // Card container — equal share of the row, full row height.
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_height(card, lv_pct(100));

    lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);          // keep black bg
    lv_obj_set_style_border_color(card, lv_color_hex(CARD_BORDER), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_pad_all(card, 6, 0);

    // Stack title over value, both centered on the main and cross axes.
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card,
                          LV_FLEX_ALIGN_CENTER,   // main axis (vertical)
                          LV_FLEX_ALIGN_CENTER,   // cross axis (horizontal)
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t* title_lbl = lv_label_create(card);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_font(title_lbl, title_font, 0);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(TITLE_COLOR), 0);

    lv_obj_t* value_lbl = lv_label_create(card);
    lv_label_set_text(value_lbl, initial_value);
    lv_obj_set_style_text_font(value_lbl, value_font, 0);
    lv_obj_set_style_text_color(value_lbl, lv_color_hex(VALUE_COLOR), 0);

    return value_lbl;
}

}  // namespace dashboard_widgets
