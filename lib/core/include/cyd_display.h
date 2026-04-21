#pragma once

#include <stdint.h>
#include "i_display.h"

class CYDDisplay : public IDisplay {
public:
    explicit CYDDisplay(uint8_t backlight_pin, uint8_t pwm_channel = 0);

    bool begin() override;
    void setBacklightPercent(uint8_t percent) override;

private:
    uint8_t backlight_pin_;
    uint8_t pwm_channel_;

    uint32_t percentToDuty(uint8_t percent) const;
};
