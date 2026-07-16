#pragma once

#include <stdint.h>
#include "i_display.h"

/**
 * Ten-step brightness stepper: 10, 20, … 100%, wrapping 100 → 10.
 *
 * Shared by the clients whose brightness button cycles levels on each tap
 * (main_hud, main_display). Deliberately separate from BrightnessController:
 * that one is fixed at four levels (25/50/75/100) and couples brightness to an
 * LDR, which suits the CYD client but neither of these.
 *
 * Knows nothing about *how* brightness is applied — it just drives an IDisplay.
 * That indirection is load-bearing: main_hud's IDisplay is LEDC PWM on a real
 * GPIO, while main_display has no PWM path at all (its backlight is a digital
 * on/off pin on an I2C expander) and instead implements setBacklightPercent()
 * as the opacity of an LVGL dim overlay. Same cycle, same button feel, very
 * different hardware underneath.
 *
 * Free of Arduino headers so it stays testable on the host.
 */
class StepBrightness {
public:
    static constexpr uint8_t LEVEL_COUNT = 10;
    static constexpr uint8_t LEVEL_STEP  = 10;  // percent per level

    // Level index at power-on. 4 → 50%, a reasonable daylight default.
    static constexpr uint8_t INITIAL_LEVEL = 4;

    explicit StepBrightness(IDisplay& display);

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
