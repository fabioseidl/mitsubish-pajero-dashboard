#pragma once

#ifndef UNIT_TEST

#include <stdint.h>
#include <esp_attr.h>  // IRAM_ATTR on the ISR declaration

/**
 * Touch half of the AXS15231B on the JC3248W535.
 *
 * The controller shares its part number with the display driver but is a separate
 * I2C peripheral on its own bus (GPIO_TOUCH_SDA/SCL). It has no register file:
 * you write an 8-byte "read touchpad" command and read back an 8-byte report.
 *
 * An INT line falls on every touch event; read() polls that flag rather than the
 * bus, so an untouched screen costs no I2C traffic.
 *
 * Coordinates come out in the panel's native portrait frame and are rotated to
 * the landscape UI frame (SCREEN_ROTATION) before being returned.
 */
class AXS15231BTouch {
public:
    AXS15231BTouch(uint8_t scl, uint8_t sda, uint8_t int_pin, uint8_t addr);

    bool begin();

    // Returns true and fills x/y (already rotated into the landscape frame) when
    // a new touch report is available; false when the screen is untouched.
    bool read(uint16_t* x, uint16_t* y);

private:
    uint8_t scl_, sda_, int_pin_, addr_;

    static void IRAM_ATTR onInterrupt();
    static AXS15231BTouch* instance_;
    static volatile bool   int_fired_;
};

#endif // UNIT_TEST
