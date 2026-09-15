#pragma once

#ifndef UNIT_TEST

#include <stdint.h>
#include <lvgl.h>

#include "i_screen_controller.h"

/**
 * The whole UI: one large speed readout, nothing else. No touch on this board.
 *
 * Optional windshield mirroring (GLASS_MIRROR_X / GLASS_MIRROR_Y build flags) is
 * applied in the flush callback, so LVGL always draws un-mirrored.
 */
class GlassScreenController : public IScreenController {
public:
    bool begin() override;
    void onPayloadReceived(const Payload& payload) override;
    void onServerStatusChanged(bool online) override;
    void tick() override;

private:
    bool initDisplay();
    void initLvgl();
    void repaint();

    static void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
    static uint32_t tickCb();

    // Written from the ESP-NOW callback, read by tick().
    volatile uint8_t speed_kmh_ = 0;
    volatile bool    online_    = false;

    // What the label currently shows; tick() repaints only on a change.
    uint8_t shown_speed_  = 0;
    bool    shown_online_ = false;

    lv_obj_t* speed_label_ = nullptr;
};

#endif // UNIT_TEST
