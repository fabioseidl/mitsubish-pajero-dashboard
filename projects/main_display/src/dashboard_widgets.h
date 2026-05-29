#pragma once

#include "lvgl/lvgl.h"

// Reusable LVGL 9.5 widget builders for the dashboard.
//
// These are pure construction helpers — they create and style widgets but hold
// no state and never update values. Data updates live in dashboard_ui.cpp.
namespace dashboard_widgets {

// Creates a borderless, transparent, non-scrollable flex container used purely
// as a layout skeleton (rows / columns). Inherits the black screen background.
lv_obj_t* make_flex(lv_obj_t* parent, lv_flex_flow_t flow);

// Builds a labelled stat block: a bordered "card" containing a small title on
// top and a large value below, both centered. The card flex-grows to share its
// row equally with sibling cards and fills the row height.
//
// Returns the value label so the caller can update it later via
// lv_label_set_text_fmt(). The title is fixed at creation time.
lv_obj_t* make_stat_block(lv_obj_t* parent,
                          const char* title,
                          const lv_font_t* title_font,
                          const lv_font_t* value_font,
                          const char* initial_value);

}  // namespace dashboard_widgets
