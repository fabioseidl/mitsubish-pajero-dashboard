#pragma once

#include "i_display.h"
#include "lgfx_config.h"

// IDisplay implementation for the Waveshare ESP32-S3-Touch-LCD-7B.
// Responsible for PCF8574 I/O expander init (LCD reset, touch reset, backlight)
// and delegating brightness changes to LovyanGFX.
//
// Call begin() BEFORE screenController.begin() so the panel is out of reset
// when lcd.init() runs.
class MainDisplay : public IDisplay {
public:
    explicit MainDisplay(LGFX& lcd);

    bool begin() override;
    void setBacklightPercent(uint8_t percent) override;

    // Re-asserts PCF8574 backlight bit. Call this after lcd.init() in case
    // Bus_RGB init disturbs the I2C bus or PCF8574 output state.
    void reapplyBacklight();

private:
    LGFX& lcd_;

    void initPCF8574();
};
