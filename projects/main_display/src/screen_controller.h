#pragma once

#include <stdint.h>
#include <freertos/FreeRTOS.h>

#include "lgfx_config.h"
#include "brightness_controller.h"
#include "payload.h"

// Owns the LVGL 9.5 display + input plumbing for the Waveshare 7-inch RGB panel
// and drives the manual dashboard UI (see dashboard_ui.h).
//
// Thread-safety: onPayload() and onConnectionChange() are called from the
// ESP-NOW WiFi task. They only buffer state; all LVGL calls are confined to
// tick(), which must run from the Arduino loop task.
class MainScreenController {
public:
    MainScreenController(LGFX& lcd, BrightnessController& brightness);

    void begin();
    void tick();

    void onPayload(const Payload& p);
    void onConnectionChange(bool online);

private:
    LGFX&                 lcd_;
    BrightnessController& brightness_;

    portMUX_TYPE  payload_mux_;
    volatile bool new_payload_;
    Payload*      pending_payload_;

    volatile bool status_dirty_;
    volatile bool status_online_;

    uint32_t last_tick_ms_;
};
