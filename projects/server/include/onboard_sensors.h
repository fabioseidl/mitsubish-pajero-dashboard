#pragma once

#include <stdint.h>
#include "data_aggregator.h"

// Qwiic I2C sensors on the server board, published into DataAggregator slots
// (PID_SENS_*) so PayloadBuilder broadcasts them like any other value:
//   MPU-6500 (0x68)        accel m/s^2 (gravity removed) + gyro rad/s (bias removed)
//   AHT20 (0x38)           temperature °C + relative humidity %RH
//   BMP280 (0x76 / 0x77)   absolute pressure hPa
// Every chip is optional; an absent one leaves its slots unset (NAN on the wire).

#ifndef ONBOARD_IMU_I2C_ADDR
#define ONBOARD_IMU_I2C_ADDR 0x68         // AD0 low (0x69 when AD0 high)
#endif
#ifndef ONBOARD_AHT20_I2C_ADDR
#define ONBOARD_AHT20_I2C_ADDR 0x38
#endif
#ifndef ONBOARD_BMP280_I2C_ADDR
#define ONBOARD_BMP280_I2C_ADDR 0x76      // SDO low
#endif
#ifndef ONBOARD_BMP280_I2C_ADDR_ALT
#define ONBOARD_BMP280_I2C_ADDR_ALT 0x77  // SDO high
#endif

/** Reads the Qwiic IMU and environment sensors into the aggregator. */
class OnboardSensors {
public:
    static constexpr uint32_t IMU_INTERVAL_MS = 100;    // matches the 10 Hz broadcast
    static constexpr uint32_t ENV_INTERVAL_MS = 1000;   // AHT20 conversion blocks ~80 ms

    /** Find the Qwiic port carrying the sensors, init each chip and calibrate the IMU. Blocking (~1 s). */
    void begin();

    /** Read whichever sensors are due and push them into the aggregator. Call often from one task. */
    void update(uint32_t now_ms, DataAggregator& aggregator);

    /** True when at least one sensor was found. */
    bool anyReady() const { return imu_ready_ || aht_ready_ || bmp_ready_; }

private:
    bool     imu_ready_       = false;
    bool     aht_ready_       = false;
    bool     bmp_ready_       = false;
    uint32_t last_imu_ms_     = 0;
    uint32_t last_env_ms_     = 0;
    float    gyro_bias_[3]    = {0.0f, 0.0f, 0.0f};   // rad/s
    float    accel_bias_[3]   = {0.0f, 0.0f, 0.0f};   // m/s^2, resting gravity vector

    /** Open Wire on the first Qwiic port that ACKs a known sensor address. */
    bool selectBus();

    /** Average resting IMU samples into gyro and accel biases; skipped if the board is moving. */
    void calibrateImu();

    /** Read accel + gyro and publish them. */
    void readImu(DataAggregator& aggregator);

    /** Read temperature, humidity and pressure and publish them. */
    void readEnv(DataAggregator& aggregator);
};
