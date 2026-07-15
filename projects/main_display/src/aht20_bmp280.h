#pragma once

#include <stdint.h>

// ─────────────────────────────────────────────────────────────
//  Environment module — AHT20 + BMP280 combo (temp / humidity / pressure)
//
//  A single breakout carrying two independent chips on the shared I2C
//  bus: an AHT20 (0x38 — temperature + humidity) and a BMP280 (0x76,
//  or 0x77 when SDO is pulled high — temperature + pressure). Driven
//  with the Adafruit_AHTX0 and Adafruit_BMP280 libraries. Readings are
//  echoed to Serial ~2 Hz for bring-up: temperature in °C, humidity in
//  %RH, pressure in hPa.
//
//  Pure consumer: begin() attaches to the already-open Wire bus and
//  probes both chips; update() must be pumped from the main loop. No
//  blocking delays, no per-loop heap. Detection is non-fatal and
//  independent per chip — if either (or both) is absent the rest of the
//  board (display, touch, MPU6050, GPS) runs on.
//
//  Bus wiring (see BOARD_SPEC.md — shared with CH422G / GT911 / MPU6050):
//    SDA = GPIO8, SCL = GPIO9. Wire.begin() is done once in setup();
//    this module never re-initialises the bus, it only uses &Wire.
// ─────────────────────────────────────────────────────────────

namespace aht20_bmp280 {

// I2C addresses. Single source of truth; override at build time.
#ifndef AHT20_I2C_ADDR
#define AHT20_I2C_ADDR 0x38        // fixed on the AHT20
#endif
#ifndef BMP280_I2C_ADDR
#define BMP280_I2C_ADDR 0x76       // SDO low
#endif
#ifndef BMP280_I2C_ADDR_ALT
#define BMP280_I2C_ADDR_ALT 0x77   // SDO high — tried if 0x76 does not answer
#endif

// Serial print cadence for the raw readings. Non-blocking (see update()).
#ifndef AHT20_BMP280_READ_INTERVAL_MS
#define AHT20_BMP280_READ_INTERVAL_MS 500   // ~2 Hz
#endif

// Attach to the shared Wire bus and probe both chips. Call once from setup().
// Non-fatal: logs and disables each chip independently if it is not found.
void begin();

// If a chip is present, read it and, ~every 500 ms, print its values.
// Call every loop iteration; it never blocks. No-op for any absent chip.
void update(uint32_t now_ms);

// True once begin() detected the respective chip on the bus.
bool aht20Ready();
bool bmp280Ready();

// Latest AHT20 ambient temperature in °C (NAN until the first read, or if the
// AHT20 is absent). Refreshed by update() at the module's read cadence.
float ambientTemperatureC();

}  // namespace aht20_bmp280
