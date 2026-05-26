#include "cyd_display.h"

#ifndef UNIT_TEST
#include <Arduino.h>
#endif

// Use a high LEDC channel number to avoid conflicts with auto-allocated channels
// (LovyanGFX, LVGL, etc. typically grab low channels starting from 0).
static constexpr uint8_t BL_LEDC_CHANNEL = 7;

CYDDisplay::CYDDisplay(int backlight_pin)
    : backlight_pin_(backlight_pin) {}

bool CYDDisplay::begin() {
#ifndef UNIT_TEST
    // Must be called AFTER the display library (LovyanGFX / TFT_eSPI) init,
    // as lgfx.init() reconfigures GPIOs and would overwrite the LEDC setup.
    //
    // Circuit is active-HIGH through a transistor: duty=0 → dark, duty=255 → full brightness.
    // Arduino-ESP32 v2.x LEDC API: ledcWrite takes the CHANNEL, not the pin.
    // Channel 7 is used to stay clear of low channels auto-allocated by other libraries.
    // 1 kHz is more reliable than 5 kHz for slow transistors on cheap backlight circuits.
    ledcSetup(BL_LEDC_CHANNEL, 1000, 8);         // channel, freq, 8-bit resolution
    ledcAttachPin(backlight_pin_, BL_LEDC_CHANNEL);

    ledcWrite(BL_LEDC_CHANNEL, percentToDuty(75));
    Serial.printf("[DISPLAY] backlight init GPIO%d ch%u — duty %lu/255\n",
                  backlight_pin_, BL_LEDC_CHANNEL, (unsigned long)percentToDuty(75));
#endif
    return true;
}

void CYDDisplay::setBacklightPercent(uint8_t percent) {
    if (percent > 100) percent = 100;
#ifndef UNIT_TEST
    if (percent != last_percent_) {
        uint32_t duty = percentToDuty(percent);
        ledcWrite(BL_LEDC_CHANNEL, duty);   // v2.x: first arg is the channel
        Serial.printf("[DISPLAY] backlight %3u%%  duty=%lu\n", percent, (unsigned long)duty);
        last_percent_ = percent;
    }
#endif
}

uint32_t CYDDisplay::percentToDuty(uint8_t percent) const {
    // 8-bit resolution, active-HIGH: 0 = off, 255 = full brightness
    return (uint32_t)(percent / 100.0f * 255);
}
