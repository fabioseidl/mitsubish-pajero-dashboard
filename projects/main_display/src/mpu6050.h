#pragma once

#include <stdint.h>

// ─────────────────────────────────────────────────────────────
//  IMU module — 6-axis accel + gyro + temperature
//
//  The board's "MPU6050" breakout actually carries an MPU-6500 die
//  (WHO_AM_I = 0x70, register-compatible with the MPU-6050 but rejected
//  by the strict Adafruit_MPU6050 driver). It is driven here with the
//  MPU6500_WE library over the board's shared I2C bus. Readings are
//  echoed to Serial ~2 Hz for bring-up: accel in m/s^2, gyro in rad/s,
//  temperature in °C (the library reports g / deg/s / °C; converted here).
//
//  Pure consumer: begin() attaches to the already-open Wire bus and
//  probes the chip; update() must be pumped from the main loop. No
//  blocking delays, no per-loop heap. Detection is non-fatal — if the
//  chip is absent the rest of the board (display, touch, GPS) runs on.
//
//  Bus wiring (see BOARD_SPEC.md — shared with CH422G / GT911):
//    SDA = GPIO8, SCL = GPIO9. Wire.begin() is done once in setup();
//    this module never re-initialises the bus, it only uses &Wire.
// ─────────────────────────────────────────────────────────────

namespace mpu6050 {

// I2C address. Single source of truth; override at build time.
#ifndef MPU6050_I2C_ADDR
#define MPU6050_I2C_ADDR 0x68   // AD0 low (0x69 when AD0 high)
#endif

// Serial print cadence for the raw readings. Non-blocking (see update()).
#ifndef MPU6050_READ_INTERVAL_MS
#define MPU6050_READ_INTERVAL_MS 500   // ~2 Hz
#endif

// Attach to the shared Wire bus and probe the chip. Call once from setup().
// Non-fatal: logs and disables the module if the IMU is not found.
void begin();

// If the chip is present, read accel/gyro/temp and, ~every 500 ms, print them.
// Call every loop iteration; it never blocks. No-op when the chip is absent.
void update(uint32_t now_ms);

// True once begin() detected the chip on the bus.
bool isReady();

// Latest cached readings (NAN until the first read, or if the chip is absent).
float accelX();          // acceleration X, m/s^2
float accelY();          // acceleration Y, m/s^2
float accelZ();          // acceleration Z, m/s^2
float accelMagnitude();  // |acceleration| vector, m/s^2 (~9.8 at rest)
float gyroX();           // angular rate X, rad/s
float gyroY();           // angular rate Y, rad/s
float gyroZ();           // angular rate Z, rad/s

}  // namespace mpu6050
