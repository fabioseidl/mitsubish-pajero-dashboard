#pragma once

#include <stdint.h>
#include "i_screen_controller.h"
#include "brightness_controller.h"

#ifndef UNIT_TEST
#include <src/lvgl.h>
#include <freertos/portmacro.h>
#else
struct lv_obj_t;
#endif

class CYDScreenController : public IScreenController {
public:
    explicit CYDScreenController(BrightnessController& brightness);

    bool begin() override;
    void onPayloadReceived(const Payload& payload) override;
    void onServerStatusChanged(bool online) override;
    void tick() override;

private:
    BrightnessController& brightness_;

    uint32_t last_tick_ms_;
    uint32_t last_touch_ms_;
    bool     touch_pressed_;

    // Pending speed update from WiFi-task callback — applied to LVGL only in tick()
    Payload  pending_payload_;
    bool     has_pending_payload_ = false;
#ifndef UNIT_TEST
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
#endif
};
