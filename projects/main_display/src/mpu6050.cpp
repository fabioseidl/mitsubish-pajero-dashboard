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

// Latest readings, cached for the dashboard (NAN until the first read).
float      accelMs2[3]   = { NAN, NAN, NAN };   // x, y, z in m/s^2
float      gyroRads[3]   = { NAN, NAN, NAN };   // x, y, z in rad/s

// Resting offsets, measured once at startup and subtracted from every reading:
//   - gyro zero-rate bias (rad/s), orientation-independent
//   - accelerometer bias = the resting gravity vector (m/s^2), so each axis and
//     the net magnitude read ~0 at rest in any mounting orientation
float      gyroBiasRads[3]  = { 0.0f, 0.0f, 0.0f };
float      accelBiasMs2[3]  = { 0.0f, 0.0f, 0.0f };

constexpr uint32_t kReadIntervalMs = MPU6050_READ_INTERVAL_MS;  // ~2 Hz

// Unit conversions — the driver reports g and deg/s; the dashboard wants SI.
constexpr float kGravityMs2 = 9.80665f;                        // 1 g → m/s^2
constexpr float kDegToRad   = 3.14159265358979323846f / 180.0f;  // deg/s → rad/s

// Measure resting sensor offsets by averaging while the board is still:
//   - gyro zero-rate bias (rad/s), always subtracted from readings
//   - accelerometer bias = the resting gravity vector (m/s^2), subtracted so the
//     per-axis readings and the net magnitude sit at ~0 at rest in any mount
//     (here the vertical mount puts ~1 g on the Y axis).
// Blocking (~0.6 s), runs once from begin(); assumes the vehicle is stationary
// at power-on. Each calibration is rejected independently if the board was
// clearly moving (gyro rate too high, or |accel| far from 1 g).
void calibrate() {
  constexpr int   kSamples     = 200;
  constexpr float kMaxBiasRads = 0.35f;   // ~20 °/s
  constexpr float kMaxAccelDev = 2.0f;    // m/s^2 tolerance around 1 g
  float gsx = 0, gsy = 0, gsz = 0;        // gyro sums (deg/s)
  float asx = 0, asy = 0, asz = 0;        // accel sums (g)
  for (int i = 0; i < kSamples; i++) {
    xyzFloat gyr = sensor.getGyrValues();   // deg/s
    xyzFloat g   = sensor.getGValues();     // g
    gsx += gyr.x; gsy += gyr.y; gsz += gyr.z;
    asx += g.x;   asy += g.y;   asz += g.z;
    delay(3);
  }

  // Gyro zero-rate bias.
  float bgx = (gsx / kSamples) * kDegToRad;
  float bgy = (gsy / kSamples) * kDegToRad;
  float bgz = (gsz / kSamples) * kDegToRad;
  if (fabsf(bgx) > kMaxBiasRads || fabsf(bgy) > kMaxBiasRads || fabsf(bgz) > kMaxBiasRads) {
    gyroBiasRads[0] = gyroBiasRads[1] = gyroBiasRads[2] = 0.0f;
    Serial.printf("[mpu6050] gyro calibration skipped — not still "
                  "(x=%.3f y=%.3f z=%.3f rad/s)\n", bgx, bgy, bgz);
  } else {
    gyroBiasRads[0] = bgx; gyroBiasRads[1] = bgy; gyroBiasRads[2] = bgz;
    Serial.printf("[mpu6050] gyro calibrated  bias[rad/s] x=%.3f y=%.3f z=%.3f\n",
                  bgx, bgy, bgz);
  }

  // Accelerometer bias = resting gravity vector (removed → ~0 per axis at rest).
  float bax = (asx / kSamples) * kGravityMs2;
  float bay = (asy / kSamples) * kGravityMs2;
  float baz = (asz / kSamples) * kGravityMs2;
  float amag = sqrtf(bax * bax + bay * bay + baz * baz);
  if (fabsf(amag - kGravityMs2) > kMaxAccelDev) {
    accelBiasMs2[0] = accelBiasMs2[1] = accelBiasMs2[2] = 0.0f;
    Serial.printf("[mpu6050] accel calibration skipped — not still (|a|=%.2f m/s^2)\n", amag);
  } else {
    accelBiasMs2[0] = bax; accelBiasMs2[1] = bay; accelBiasMs2[2] = baz;
    Serial.printf("[mpu6050] accel calibrated  bias[m/s^2] x=%.2f y=%.2f z=%.2f\n",
                  bax, bay, baz);
  }
}

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

  // Gyro + accel resting calibration (keep the board still at power-on).
  calibrate();
}

void update(uint32_t now_ms) {
  if (!ready) return;   // chip absent — nothing to do

  if (now_ms - lastReadMs < kReadIntervalMs) return;
  lastReadMs = now_ms;

  // Cache the readings for the dashboard. Driver units are g and deg/s; convert
  // to SI (m/s^2, rad/s) here.
  xyzFloat g   = sensor.getGValues();
  xyzFloat gyr = sensor.getGyrValues();
  accelMs2[0] = g.x   * kGravityMs2 - accelBiasMs2[0];
  accelMs2[1] = g.y   * kGravityMs2 - accelBiasMs2[1];
  accelMs2[2] = g.z   * kGravityMs2 - accelBiasMs2[2];
  gyroRads[0] = gyr.x * kDegToRad - gyroBiasRads[0];
  gyroRads[1] = gyr.y * kDegToRad - gyroBiasRads[1];
  gyroRads[2] = gyr.z * kDegToRad - gyroBiasRads[2];
}

bool isReady() { return ready; }

float accelX() { return accelMs2[0]; }   // m/s^2
float accelY() { return accelMs2[1]; }   // m/s^2
float accelZ() { return accelMs2[2]; }   // m/s^2
float gyroX()  { return gyroRads[0]; }   // rad/s
float gyroY()  { return gyroRads[1]; }   // rad/s
float gyroZ()  { return gyroRads[2]; }   // rad/s

// Magnitude of the acceleration vector, m/s^2 (~9.8 at rest).
float accelMagnitude() {
  return sqrtf(accelMs2[0] * accelMs2[0] +
               accelMs2[1] * accelMs2[1] +
               accelMs2[2] * accelMs2[2]);
}

}  // namespace mpu6050
