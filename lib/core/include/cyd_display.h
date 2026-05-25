#pragma once

#include <stdint.h>
#include "i_display.h"

class CYDDisplay : public IDisplay {
public:
    explicit CYDDisplay(int backlight_pin);

    bool begin() override;
    void setBacklightPercent(uint8_t percent) override;

private:
    int     backlight_pin_;
    uint8_t last_percent_ = 255; // sentinel: forces first write through

    uint32_t percentToDuty(uint8_t percent) const;
};
