#include "onboard_sensors.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <MPU6500_WE.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_Sensor.h>

#include "pid_map.h"
#include "pin_config.h"

namespace {

MPU6500_WE      g_imu(&Wire, ONBOARD_IMU_I2C_ADDR);
Adafruit_AHTX0  g_aht;
Adafruit_BMP280 g_bmp(&Wire);

constexpr uint32_t kI2cClockHz = 400000;
constexpr float    kGravityMs2 = 9.80665f;                            // 1 g → m/s^2
constexpr float    kDegToRad   = 3.14159265358979323846f / 180.0f;    // deg/s → rad/s
constexpr float    kPaToHpa    = 0.01f;

/** True when a device ACKs addr on the currently open Wire bus. */
bool probe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

/** True when any of the expected sensor chips answers on the open bus. */
bool anySensorAnswers() {
    return probe(ONBOARD_IMU_I2C_ADDR) || probe(ONBOARD_AHT20_I2C_ADDR) ||
           probe(ONBOARD_BMP280_I2C_ADDR) || probe(ONBOARD_BMP280_I2C_ADDR_ALT);
}

}  // namespace

bool OnboardSensors::selectBus() {
    struct Port { int sda; int scl; const char* name; };
    static const Port kPorts[] = {
        { PIN_QWIIC_A_SDA, PIN_QWIIC_A_SCL, "Qwiic A" },
        { PIN_QWIIC_B_SDA, PIN_QWIIC_B_SCL, "Qwiic B" },
    };
    for (const Port& port : kPorts) {
        Wire.begin(port.sda, port.scl, kI2cClockHz);
        if (anySensorAnswers()) {
            Serial.printf("[sensors] I2C on %s (SDA=%d SCL=%d)\n", port.name, port.sda, port.scl);
            return true;
        }
        Wire.end();
    }
    Serial.println("[sensors] no sensor answered on either Qwiic port");
    return false;
}

void OnboardSensors::begin() {
    if (!selectBus()) return;

    // MPU-6500 (the "MPU6050" breakout carries a 6500 die, WHO_AM_I = 0x70)
    imu_ready_ = g_imu.init();
    if (imu_ready_) {
        g_imu.setAccRange(MPU6500_ACC_RANGE_4G);
        g_imu.setGyrRange(MPU6500_GYRO_RANGE_500);
        Serial.printf("[sensors] MPU-6500 OK at 0x%02X\n", ONBOARD_IMU_I2C_ADDR);
        calibrateImu();
    } else {
        Serial.printf("[sensors] MPU-6500 not found at 0x%02X (WHO_AM_I=0x%02X)\n",
                      ONBOARD_IMU_I2C_ADDR, g_imu.whoAmI());
    }

    aht_ready_ = g_aht.begin(&Wire, 0, ONBOARD_AHT20_I2C_ADDR);
    Serial.printf("[sensors] AHT20 %s at 0x%02X\n", aht_ready_ ? "OK" : "not found",
                  ONBOARD_AHT20_I2C_ADDR);

    uint8_t bmp_addr = ONBOARD_BMP280_I2C_ADDR;
    bmp_ready_ = g_bmp.begin(bmp_addr);
    if (!bmp_ready_) {
        bmp_addr   = ONBOARD_BMP280_I2C_ADDR_ALT;
        bmp_ready_ = g_bmp.begin(bmp_addr);
    }
    if (bmp_ready_) {
        g_bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                          Adafruit_BMP280::SAMPLING_X2,     // temperature
                          Adafruit_BMP280::SAMPLING_X16,    // pressure
                          Adafruit_BMP280::FILTER_X16,
                          Adafruit_BMP280::STANDBY_MS_500);
        Serial.printf("[sensors] BMP280 OK at 0x%02X\n", bmp_addr);
    } else {
        Serial.println("[sensors] BMP280 not found at 0x76/0x77");
    }
}

void OnboardSensors::calibrateImu() {
    constexpr int   kSamples     = 200;
    constexpr float kMaxBiasRads = 0.35f;   // ~20 deg/s — above this the board is turning
    constexpr float kMaxAccelDev = 2.0f;    // m/s^2 tolerance around 1 g
    float gs[3] = {0, 0, 0};                // deg/s sums
    float as[3] = {0, 0, 0};                // g sums
    for (int i = 0; i < kSamples; ++i) {
        xyzFloat gyr = g_imu.getGyrValues();
        xyzFloat acc = g_imu.getGValues();
        gs[0] += gyr.x; gs[1] += gyr.y; gs[2] += gyr.z;
        as[0] += acc.x; as[1] += acc.y; as[2] += acc.z;
        delay(3);
    }

    float bg[3], ba[3];
    for (int i = 0; i < 3; ++i) {
        bg[i] = gs[i] / kSamples * kDegToRad;
        ba[i] = as[i] / kSamples * kGravityMs2;
    }

    if (fabsf(bg[0]) > kMaxBiasRads || fabsf(bg[1]) > kMaxBiasRads || fabsf(bg[2]) > kMaxBiasRads) {
        Serial.printf("[sensors] gyro calibration skipped, board moving (%.3f %.3f %.3f rad/s)\n",
                      bg[0], bg[1], bg[2]);
    } else {
        for (int i = 0; i < 3; ++i) gyro_bias_[i] = bg[i];
        Serial.printf("[sensors] gyro bias %.3f %.3f %.3f rad/s\n", bg[0], bg[1], bg[2]);
    }

    // Accel bias is the resting gravity vector, so each axis reads ~0 at rest in any mount.
    float amag = sqrtf(ba[0] * ba[0] + ba[1] * ba[1] + ba[2] * ba[2]);
    if (fabsf(amag - kGravityMs2) > kMaxAccelDev) {
        Serial.printf("[sensors] accel calibration skipped, |a|=%.2f m/s^2\n", amag);
    } else {
        for (int i = 0; i < 3; ++i) accel_bias_[i] = ba[i];
        Serial.printf("[sensors] accel bias %.2f %.2f %.2f m/s^2\n", ba[0], ba[1], ba[2]);
    }
}

void OnboardSensors::update(uint32_t now_ms, DataAggregator& aggregator) {
    if (imu_ready_ && now_ms - last_imu_ms_ >= IMU_INTERVAL_MS) {
        last_imu_ms_ = now_ms;
        readImu(aggregator);
    }
    if ((aht_ready_ || bmp_ready_) && now_ms - last_env_ms_ >= ENV_INTERVAL_MS) {
        last_env_ms_ = now_ms;
        readEnv(aggregator);
    }
}

void OnboardSensors::readImu(DataAggregator& aggregator) {
    xyzFloat acc = g_imu.getGValues();      // g
    xyzFloat gyr = g_imu.getGyrValues();    // deg/s
    aggregator.update(PID_SENS_ACCEL_X, acc.x * kGravityMs2 - accel_bias_[0]);
    aggregator.update(PID_SENS_ACCEL_Y, acc.y * kGravityMs2 - accel_bias_[1]);
    aggregator.update(PID_SENS_ACCEL_Z, acc.z * kGravityMs2 - accel_bias_[2]);
    aggregator.update(PID_SENS_GYRO_X,  gyr.x * kDegToRad   - gyro_bias_[0]);
    aggregator.update(PID_SENS_GYRO_Y,  gyr.y * kDegToRad   - gyro_bias_[1]);
    aggregator.update(PID_SENS_GYRO_Z,  gyr.z * kDegToRad   - gyro_bias_[2]);
}

void OnboardSensors::readEnv(DataAggregator& aggregator) {
    if (aht_ready_) {
        sensors_event_t humidity, temp;
        if (g_aht.getEvent(&humidity, &temp)) {
            aggregator.update(PID_SENS_ENV_TEMP,     temp.temperature);
            aggregator.update(PID_SENS_ENV_HUMIDITY, humidity.relative_humidity);
        }
    }
    if (bmp_ready_) {
        float pa = g_bmp.readPressure();
        if (!isnan(pa) && pa > 0.0f) {
            aggregator.update(PID_SENS_ENV_PRESSURE, pa * kPaToHpa);
        }
    }
}
