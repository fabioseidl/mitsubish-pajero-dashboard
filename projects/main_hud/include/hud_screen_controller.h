#pragma once

#ifndef UNIT_TEST

#include <stdint.h>
#include <lvgl.h>

#include "i_screen_controller.h"
#include "hud_brightness.h"
#include "axs15231b_touch.h"

/**
 * The whole UI: one very large speed readout plus two touch buttons.
 *
 * Display modes
 * -------------
 *  SIMPLE — read the panel directly.
 *  HUD    — panel lies face-up and reflects off the windshield, so the frame is
 *           mirrored left-to-right on its way to the glass (MIRROR_HORIZONTAL).
 *
 * Mirroring happens in the LVGL flush callback rather than in the panel or the
 * widget tree: the AXS15231B has no hardware mirror, and LVGL has no flip
 * transform for a whole display. LVGL therefore always draws un-mirrored and
 * never knows which mode is active — which also means touch input has to be
 * mirrored back the other way (see touchReadCb) so buttons stay where the
 * user sees them.
 */
class HudScreenController : public IScreenController {
public:
    enum class Mode : uint8_t { SIMPLE = 0, HUD = 1 };

    explicit HudScreenController(HudBrightness& brightness);

    bool begin() override;
    void onPayloadReceived(const Payload& payload) override;
    void onServerStatusChanged(bool online) override;
    void tick() override;

    Mode getMode() const { return mode_; }

private:
    // Which way HUD mode flips the image. Horizontal (left-right) suits a
    // face-up panel whose top edge points at the windshield; change these if
    // the reflection reads backwards once mounted.
    static constexpr bool MIRROR_HORIZONTAL = true;
    static constexpr bool MIRROR_VERTICAL   = false;

    // How long a touch report keeps LVGL in the PRESSED state. The controller
    // reports on interrupt only, so without a hold window LVGL would see a
    // single-frame press and could miss the click.
    static constexpr uint32_t TOUCH_HOLD_MS = 80;

    bool initDisplay();
    bool initLvgl();
    void buildUi();
    void setMode(Mode mode);
    void refreshModeButton();
    void refreshBrightnessButton();
    void repaint();

    // LVGL C callbacks — they reach the live instance through instance_.
    static void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
    static void touchReadCb(lv_indev_t* indev, lv_indev_data_t* data);
    static uint32_t tickCb();
    static void onModeButton(lv_event_t* e);
    static void onBrightnessButton(lv_event_t* e);

    static HudScreenController* instance_;

    HudBrightness&  brightness_;
    AXS15231BTouch  touch_;

    Mode     mode_      = Mode::SIMPLE;

    // Written from the ESP-NOW callback, read by tick().
    volatile uint8_t speed_kmh_ = 0;
    volatile bool    online_    = false;

    // What the widgets currently say; tick() repaints only on a change.
    uint8_t shown_speed_  = 0;
    bool    shown_online_ = false;

    // Latched touch state (see TOUCH_HOLD_MS).
    uint16_t touch_x_       = 0;
    uint16_t touch_y_       = 0;
    uint32_t touch_until_ms_ = 0;

    lv_obj_t* speed_label_      = nullptr;
    lv_obj_t* unit_label_       = nullptr;
    lv_obj_t* mode_btn_label_   = nullptr;
    lv_obj_t* bright_btn_label_ = nullptr;
};

#endif // UNIT_TEST
