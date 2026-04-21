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

private:
    IDisplay& display_;
    uint8_t   current_level_;
};
