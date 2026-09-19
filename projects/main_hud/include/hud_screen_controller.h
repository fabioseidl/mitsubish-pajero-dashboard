#pragma once

#ifndef UNIT_TEST

#include <stdint.h>
#include <lvgl.h>

#include "i_screen_controller.h"
#include "step_brightness.h"
#include "axs15231b_touch.h"

/**
 * The whole UI: one very large speed readout, a brightness readout, and a pair
 * of +/- brightness buttons.
 *
 * The panel is read directly — LVGL's frame reaches the canvas untransformed.
 */
class HudScreenController : public IScreenController {
public:
    explicit HudScreenController(StepBrightness& brightness);

    bool begin() override;
    void onPayloadReceived(const Payload& payload) override;
    void onServerStatusChanged(bool online) override;
    void tick() override;

private:
    // How long a touch report keeps LVGL in the PRESSED state. The controller
    // reports on interrupt only, so without a hold window LVGL would see a
    // single-frame press and could miss the click.
    static constexpr uint32_t TOUCH_HOLD_MS = 80;

    // Buttons hide this long after the last touch, leaving the speed alone on
    // screen. They are shown at boot so they can be discovered.
    static constexpr uint32_t BUTTONS_IDLE_HIDE_MS = 3000;

    bool initDisplay();
    bool initLvgl();
    void buildUi();
    void refreshBrightnessLabel();
    void setButtonsVisible(bool visible);
    void repaint();

    // LVGL C callbacks — they reach the live instance through instance_.
    static void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
    static void touchReadCb(lv_indev_t* indev, lv_indev_data_t* data);
    static uint32_t tickCb();
    static void onBrightnessUp(lv_event_t* e);
    static void onBrightnessDown(lv_event_t* e);

    static HudScreenController* instance_;

    StepBrightness&  brightness_;
    AXS15231BTouch  touch_;

    // Written from the ESP-NOW callback, read by tick().
    volatile uint8_t speed_kmh_ = 0;
    volatile bool    online_    = false;

    // What the widgets currently say; tick() repaints only on a change.
    uint8_t shown_speed_  = 0;
    bool    shown_online_ = false;

    // Set by flushCb when LVGL touches the canvas. Pushing the canvas costs a
    // full 307 KB QSPI transfer (~15 ms), so tick() only does it when something
    // actually changed rather than on every loop.
    bool canvas_dirty_ = false;

    // Latched touch state (see TOUCH_HOLD_MS).
    uint16_t touch_x_        = 0;
    uint16_t touch_y_        = 0;
    uint32_t touch_until_ms_ = 0;
    uint32_t last_touch_ms_  = 0;

    // Set at the start of a gesture that began while the buttons were hidden.
    // Such a gesture only wakes the buttons and is never reported to LVGL as a
    // press — otherwise the tap that reveals the buttons would also hit whatever
    // button appeared under the finger.
    bool wake_only_ = false;

    // Buttons start visible so they can be found; tick() hides them once
    // BUTTONS_IDLE_HIDE_MS passes with no touch.
    bool buttons_visible_ = true;

    lv_obj_t* speed_label_      = nullptr;
    lv_obj_t* bright_up_btn_    = nullptr;
    lv_obj_t* bright_down_btn_  = nullptr;
    lv_obj_t* bright_pct_label_ = nullptr;
};

#endif // UNIT_TEST
