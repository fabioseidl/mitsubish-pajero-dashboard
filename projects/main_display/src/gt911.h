#pragma once

#include <stdint.h>

/**
 * Goodix GT911 capacitive touch controller (BOARD_SPEC §6).
 *
 * Sits on the board's shared I2C bus (SDA=8 / SCL=9) alongside the PCF8574,
 * MPU6050 and AHT20/BMP280, and uses the same Wire instance as the rest of the
 * app — a second I2C master (e.g. LovyanGFX's own) on the same pins would fight
 * with it.
 *
 * Reset is PCF8574 P2, so begin() must run *after* the expander has driven P2
 * high; the chip is invisible on the bus while held in reset (which is why the
 * i2c_scan() in setup() does not list it).
 *
 * The INT pin is not used. BOARD_SPEC calls it "project-dependent" and it is not
 * identified on this board, so read() polls the status register instead — at the
 * LVGL input rate that costs one short I2C transaction per read and removes the
 * need to know the pin at all.
 */
namespace gt911 {

// Probes 0x5D then 0x14 and verifies the product ID reads back as "911".
// Returns false if the chip does not answer — touch is then simply absent and
// the rest of the UI keeps working.
bool begin();

// Same probe without logging, for callers running a scan over many candidates.
bool detect();

// True while a finger is down, with x/y already mapped to the 1024x600 panel.
bool read(uint16_t* x, uint16_t* y);

// I2C address that answered, or 0 when begin() failed.
uint8_t address();

}  // namespace gt911
