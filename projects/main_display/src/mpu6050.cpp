// ============================================================
//  IMU module — MPU-6500 6-axis accel + gyro (see mpu6050.h)
//
//  Probe (MPU6500_WE) → read accel/gyro/temp → convert → format.
//  Shares the board's I2C bus (SDA=8, SCL=9) with the CH422G IO
//  expander and the GT911 touch controller; Wire.begin() is done
//  once in setup(), so this module only passes &Wire to the driver
//  (MPU6500_WE never re-runs Wire.begin() in I2C mode).
// ============================================================

#include "mpu6050.h"

#include <Arduino.h>
#include <Wire.h>
#include <MPU6500_WE.h>

namespace mpu6050 {
namespace {

// ── Static state (no per-loop allocation) ────────────────────
MPU6500_WE sensor(&Wire, MPU6050_I2C_ADDR);
bool       ready         = false;
uint32_t   lastReadMs    = 0;

constexpr uint32_t kReadIntervalMs = MPU6050_READ_INTERVAL_MS;  // ~2 Hz

// Unit conversions — the driver reports g and deg/s; the dashboard wants SI.
constexpr float kGravityMs2 = 9.80665f;                        // 1 g → m/s^2
constexpr float kDegToRad   = 3.14159265358979323846f / 180.0f;  // deg/s → rad/s

}  // namespace

void begin() {
  // init() probes WHO_AM_I (expects 0x70 for the MPU6500) over the already-open
  // Wire bus — do NOT call Wire.begin() here; it is initialised once in setup()
  // and shared with CH422G / GT911.
  if (!sensor.init()) {
    ready = false;
    Serial.printf("[mpu6050] init failed at 0x%02X — WHO_AM_I=0x%02X "
                  "(expected 0x70 MPU6500) (non-fatal)\n",
                  MPU6050_I2C_ADDR, sensor.whoAmI());
    return;
  }
  // Full-scale ranges: ±4 g and ±500 deg/s — a sane default for dashboard tilt.
  sensor.setAccRange(MPU6500_ACC_RANGE_4G);
  sensor.setGyrRange(MPU6500_GYRO_RANGE_500);
  ready = true;
  Serial.printf("[mpu6050] init OK  addr=0x%02X  WHO_AM_I=0x%02X (MPU6500)  "
                "(accel m/s^2, gyro rad/s, temp C)\n",
                MPU6050_I2C_ADDR, sensor.whoAmI());
}

void update(uint32_t now_ms) {
  if (!ready) return;   // chip absent — nothing to do

  if (now_ms - lastReadMs < kReadIntervalMs) return;
  lastReadMs = now_ms;

  // Driver units: accel in g, gyro in deg/s, temp in °C. Convert to SI below.
  xyzFloat g    = sensor.getGValues();
  xyzFloat gyr  = sensor.getGyrValues();
  float    tempC = sensor.getTemperature();

  Serial.printf("[mpu6050] accel[m/s^2] x=%.2f y=%.2f z=%.2f  "
                "gyro[rad/s] x=%.2f y=%.2f z=%.2f  temp=%.2fC\n",
                g.x * kGravityMs2, g.y * kGravityMs2, g.z * kGravityMs2,
                gyr.x * kDegToRad, gyr.y * kDegToRad, gyr.z * kDegToRad,
                tempC);
}

bool isReady() { return ready; }

}  // namespace mpu6050
