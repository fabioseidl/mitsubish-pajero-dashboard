#pragma once

#include <stdint.h>
#include "i_display.h"

/**
 * Backlight control for the JC3248W535.
 *
 * Same job as lib/core's CYDDisplay but kept local: that class is named for the
 * CYD board and hardcodes a 75% startup level. This one starts at HUD_INITIAL_PERCENT
 * and drives GPIO_BACKLIGHT_PIN, which is active-HIGH straight off the SoC (no
 * transistor), so a higher PWM frequency is fine here.
 */
class HudDisplay : public IDisplay {
public:
    explicit HudDisplay(int backlight_pin);

    bool begin() override;
    void setBacklightPercent(uint8_t percent) override;

private:
    int     backlight_pin_;
    uint8_t last_percent_ = 255;  // sentinel: forces the first write through

    uint32_t percentToDuty(uint8_t percent) const;
};
