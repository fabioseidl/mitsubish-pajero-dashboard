#pragma once

#include <stdint.h>
#include "i_display.h"

class BrightnessController {
public:
    static constexpr uint8_t LEVEL_COUNT = 4;
    static constexpr uint8_t LEVEL_PERCENTS[LEVEL_COUNT] = {25, 50, 75, 100};

    explicit BrightnessController(IDisplay& display);

    void onTouch();
    uint8_t getCurrentLevel() const;
    uint8_t getCurrentPercent() const;
    void applyInitial();

    // Feed a raw ADC sample (0–4095) from the LDR; adjusts backlight smoothly.
    // High ADC value = bright environment → high backlight, and vice versa.
    // If the LDR circuit inverts the signal (bright = low ADC), flip the mapping
    // by setting LDR_INVERT = true in brightness_controller.cpp.
    void onLdrReading(uint16_t raw_adc);

    // Returns the last backlight % applied by the LDR (0 if not yet sampled).
    uint8_t getLdrPercent() const;

private:
    IDisplay& display_;
    uint8_t   current_level_;

    // Exponential moving average state for LDR smoothing
    float   ldr_ema_;
    uint8_t ldr_percent_; // last backlight % applied by the LDR

    static constexpr float    LDR_EMA_ALPHA = 0.10f; // ~10 samples to converge
    static constexpr uint16_t LDR_ADC_MIN  = 0;     // floor  → maximum backlight
    static constexpr uint16_t LDR_ADC_MAX  = 50;    // ceiling → minimum backlight
    static constexpr uint8_t  BL_PCT_MIN   = 10;    // minimum backlight %
    static constexpr bool     LDR_INVERT   = true;  // raw=0 → bright, raw≥50 → dim
};
