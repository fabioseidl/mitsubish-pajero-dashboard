#pragma once

#include <stdint.h>
#include <lvgl.h>

/**
 * The HUD's LVGL widgets: speed readout, brightness percent and the +/- buttons.
 *
 * Pure LVGL — no Arduino, panel or touch code — so the same file builds for the
 * board and for the native simulator (sim/). HudScreenController owns the
 * hardware and drives this view.
 */
class HudUi {
public:
    /** Button handler; ctx is the pointer passed to build(). */
    using Callback = void (*)(void* ctx);

    /** Creates every widget on scr. on_up/on_down fire on the +/- buttons. */
    void build(lv_obj_t* scr, Callback on_up, Callback on_down, void* ctx);

    /** Shows the speed in white, or grey "--" when the server is offline. */
    void setSpeed(uint8_t kmh, bool online);

    /** Updates the brightness percent between the buttons. */
    void setBrightnessPercent(uint8_t percent);

    /** Shows or hides the brightness row; the speed stays either way. */
    void setButtonsVisible(bool visible);

    /** Whether the brightness row is currently shown. */
    bool buttonsVisible() const { return buttons_visible_; }

private:
    /** LVGL click handlers; event user_data is the HudUi instance. */
    static void onUpClicked(lv_event_t* e);
    static void onDownClicked(lv_event_t* e);

    Callback on_up_   = nullptr;
    Callback on_down_ = nullptr;
    void*    ctx_     = nullptr;

    bool buttons_visible_ = true;

    lv_obj_t* speed_label_      = nullptr;
    lv_obj_t* bright_up_btn_    = nullptr;
    lv_obj_t* bright_down_btn_  = nullptr;
    lv_obj_t* bright_pct_label_ = nullptr;
};
