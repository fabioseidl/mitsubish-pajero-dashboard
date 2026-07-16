#include "hud_display.h"

#ifndef UNIT_TEST
#include <Arduino.h>
#endif

// High channel number, clear of any low channels other libraries auto-allocate.
static constexpr uint8_t BL_LEDC_CHANNEL = 7;
static constexpr uint32_t BL_LEDC_FREQ_HZ = 5000;

HudDisplay::HudDisplay(int backlight_pin)
    : backlight_pin_(backlight_pin) {}

bool HudDisplay::begin() {
#ifndef UNIT_TEST
    // Call after Arduino_GFX init: gfx->begin() reconfigures GPIOs and would
    // otherwise clobber the LEDC attachment.
    // Arduino-ESP32 v2.x LEDC API: ledcWrite takes the CHANNEL, not the pin.
    ledcSetup(BL_LEDC_CHANNEL, BL_LEDC_FREQ_HZ, 8);  // channel, freq, 8-bit resolution
    ledcAttachPin(backlight_pin_, BL_LEDC_CHANNEL);

    Serial.printf("[DISPLAY] backlight init GPIO%d ch%u\n", backlight_pin_, BL_LEDC_CHANNEL);
#endif
    return true;
}

void HudDisplay::setBacklightPercent(uint8_t percent) {
    if (percent > 100) percent = 100;
#ifndef UNIT_TEST
    if (percent != last_percent_) {
        uint32_t duty = percentToDuty(percent);
        ledcWrite(BL_LEDC_CHANNEL, duty);  // v2.x: first arg is the channel
        Serial.printf("[DISPLAY] backlight %3u%%  duty=%lu\n", percent, (unsigned long)duty);
        last_percent_ = percent;
    }
#endif
}

uint32_t HudDisplay::percentToDuty(uint8_t percent) const {
    // 8-bit resolution, active-HIGH: 0 = off, 255 = full brightness.
    return (uint32_t)(percent / 100.0f * 255);
}
