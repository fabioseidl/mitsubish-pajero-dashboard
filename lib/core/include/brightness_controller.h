#pragma once

#include <stdint.h>
#include "i_display.h"

class BrightnessController {
public:
    static constexpr uint8_t LEVEL_COUNT = 4;
    static constexpr uint8_t LEVEL_PERCENTS[LEVEL_COUNT] = {25, 50, 75, 100};

    explicit BrightnessController(IDisplay& display);

    // now_ms — current uptime in milliseconds (from esp_timer / millis()).
    // Recorded so the LDR auto-brightness is suppressed for MANUAL_OVERRIDE_MS
    // after a manual touch/button press, preventing it from immediately overriding
    // the user's chosen brightness level.
    void onTouch(uint32_t now_ms = 0);
    uint8_t getCurrentLevel() const;
    uint8_t getCurrentPercent() const;
    void applyInitial();

    // Feed a raw ADC sample (0–4095) from the LDR; adjusts backlight smoothly.
    // With LDR_INVERT=true (pull-down wiring): raw=0 → bright outdoors → high
    // backlight; raw=4095 → dark indoors → low backlight.
    // Ignored for MANUAL_OVERRIDE_MS after the last onTouch() call.
    void onLdrReading(uint16_t raw_adc, uint32_t now_ms = 0);

    // Returns the last backlight % applied by the LDR (0 if not yet sampled).
    uint8_t getLdrPercent() const;

private:
    IDisplay& display_;
    uint8_t   current_level_;

    // Exponential moving average state for LDR smoothing
    float   ldr_ema_;
    uint8_t ldr_percent_; // last backlight % applied by the LDR

    // Uptime (ms) until which LDR readings are suppressed after a manual touch.
    uint32_t manual_override_until_ms_ = 0;

    static constexpr uint32_t  MANUAL_OVERRIDE_MS = 10000; // 10 s hold-off after touch
    static constexpr float     LDR_EMA_ALPHA = 0.25f;      // ~4 samples (~2 s) to converge
    static constexpr uint16_t  LDR_ADC_MIN  = 0;           // ADC floor
    static constexpr uint16_t  LDR_ADC_MAX  = 300;         // ADC ceiling — raw ≥ 300 clamps to BL_PCT_MIN
    static constexpr uint8_t   BL_PCT_MIN   = 5;           // backlight % when sensor is at LDR_ADC_MAX
    static constexpr uint8_t   BL_PCT_MAX   = 30;          // backlight % when sensor is at LDR_ADC_MIN
    static constexpr bool      LDR_INVERT   = true;        // true → dark env = high backlight, bright env = dim
};
