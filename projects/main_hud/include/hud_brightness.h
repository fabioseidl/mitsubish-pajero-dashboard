#pragma once

#include <stdint.h>
#include "i_display.h"

/**
 * Ten-step backlight stepper: 10, 20, … 100%, wrapping 100 → 10.
 *
 * Deliberately not lib/core's BrightnessController: that one is fixed at four
 * levels (25/50/75/100) and couples brightness to an LDR, neither of which suits
 * this board (no light sensor, and a HUD projection needs fine control at the
 * dim end for night driving). Kept free of Arduino headers so it stays testable
 * on the host against a mock IDisplay.
 */
class HudBrightness {
public:
    static constexpr uint8_t LEVEL_COUNT = 10;
    static constexpr uint8_t LEVEL_STEP  = 10;  // percent per level

    // Level index at power-on. 4 → 50%, a reasonable daylight default.
    static constexpr uint8_t INITIAL_LEVEL = 4;

    explicit HudBrightness(IDisplay& display);

    // Advance one step and apply it; wraps from 100% back to 10%.
    void next();

    // Push the current level to the display without changing it.
    void applyCurrent();

    uint8_t getCurrentLevel() const { return level_; }
    uint8_t getCurrentPercent() const { return percentFor(level_); }

private:
    static constexpr uint8_t percentFor(uint8_t level) {
        return (uint8_t)((level + 1) * LEVEL_STEP);
    }

    IDisplay& display_;
    uint8_t   level_ = INITIAL_LEVEL;
};
