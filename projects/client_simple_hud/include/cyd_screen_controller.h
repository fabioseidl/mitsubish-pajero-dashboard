#pragma once

#include <stdint.h>
#include "i_screen_controller.h"
#include "brightness_controller.h"

#ifndef UNIT_TEST
// FreeRTOS.h must be included before portmacro.h so that portUSING_MPU_WRAPPERS
// is not defined twice (FreeRTOS.h sets it to 0 first, portmacro.h to 1 later).
// Including FreeRTOS.h here pulls in portmacro.h in the right order; subsequent
// includes of Arduino.h → FreeRTOS.h are then no-ops due to header guards.
#include <freertos/FreeRTOS.h>
#include <src/lvgl.h>
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

    // Physical BOOT button state
    bool     btn_pressed_;
    uint32_t last_btn_ms_;

    // Pending payload from WiFi-task callback — applied to LVGL only in tick().
    // Stored as a heap pointer so the 221-byte struct does not inflate .dram0.bss
    // (the CYDScreenController object is a global static in main.cpp).
    Payload* pending_payload_     = nullptr;
    bool     has_pending_payload_ = false;
#ifndef UNIT_TEST
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
#endif
};
