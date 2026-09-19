#include "cyd_display.h"

#ifndef UNIT_TEST
#include <Arduino.h>
#endif

// LEDC backlight control, portable across Arduino-ESP32 cores.
//
// Core 3.x reworked the LEDC API: channels are allocated internally and every call
// addresses the PIN, so ledcSetup()+ledcAttachPin() collapsed into ledcAttach()
// and ledcWrite() now takes the pin. Core 2.x still needs an explicit channel.
// `platform = espressif32` is unpinned in the sub-projects, so this file has to
// build under either core — hence the shim rather than a one-way rewrite.
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  #define BL_LEDC_CORE3 1
#else
  #define BL_LEDC_CORE3 0
  // Use a high LEDC channel number to avoid conflicts with auto-allocated channels
  // (LovyanGFX, LVGL, etc. typically grab low channels starting from 0).
  static constexpr uint8_t BL_LEDC_CHANNEL = 7;
#endif

static constexpr uint32_t BL_LEDC_FREQ_HZ = 1000;   // slow transistors dislike 5 kHz
static constexpr uint8_t  BL_LEDC_BITS    = 8;      // duty 0..255

CYDDisplay::CYDDisplay(int backlight_pin)
    : backlight_pin_(backlight_pin) {}

bool CYDDisplay::begin() {
#ifndef UNIT_TEST
    // Must be called AFTER the display library (LovyanGFX / TFT_eSPI) init,
    // as lgfx.init() reconfigures GPIOs and would overwrite the LEDC setup.
    //
    // Circuit is active-HIGH through a transistor: duty=0 → dark, duty=255 → full brightness.
#if BL_LEDC_CORE3
    ledcAttach(backlight_pin_, BL_LEDC_FREQ_HZ, BL_LEDC_BITS);
    ledcWrite(backlight_pin_, percentToDuty(75));
    Serial.printf("[DISPLAY] backlight init GPIO%d — duty %lu/255\n",
                  backlight_pin_, (unsigned long)percentToDuty(75));
#else
    ledcSetup(BL_LEDC_CHANNEL, BL_LEDC_FREQ_HZ, BL_LEDC_BITS);   // channel, freq, resolution
    ledcAttachPin(backlight_pin_, BL_LEDC_CHANNEL);
    ledcWrite(BL_LEDC_CHANNEL, percentToDuty(75));
    Serial.printf("[DISPLAY] backlight init GPIO%d ch%u — duty %lu/255\n",
                  backlight_pin_, BL_LEDC_CHANNEL, (unsigned long)percentToDuty(75));
#endif
#endif
    return true;
}

void CYDDisplay::setBacklightPercent(uint8_t percent) {
    if (percent > 100) percent = 100;
#ifndef UNIT_TEST
    if (percent != last_percent_) {
        uint32_t duty = percentToDuty(percent);
#if BL_LEDC_CORE3
        ledcWrite(backlight_pin_, duty);    // 3.x: addressed by pin
#else
        ledcWrite(BL_LEDC_CHANNEL, duty);   // 2.x: addressed by channel
#endif
        Serial.printf("[DISPLAY] backlight %3u%%  duty=%lu\n", percent, (unsigned long)duty);
        last_percent_ = percent;
    }
#endif
}

uint32_t CYDDisplay::percentToDuty(uint8_t percent) const {
    // 8-bit resolution, active-HIGH: 0 = off, 255 = full brightness
    return (uint32_t)(percent / 100.0f * 255);
}
