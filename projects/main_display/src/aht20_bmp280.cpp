// ============================================================
//  Environment module — AHT20 + BMP280 combo (see aht20_bmp280.h)
//
//  Probe (Adafruit_AHTX0 / Adafruit_BMP280) → read → format.
//  Two independent chips on one breakout; each is detected and read
//  independently. Shares the board's I2C bus (SDA=8, SCL=9) with the
//  CH422G IO expander, the GT911 touch controller and the MPU6050;
//  Wire.begin() is done once in setup(), so this module only passes
//  &Wire to the drivers.
// ============================================================

#include "aht20_bmp280.h"

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_Sensor.h>

namespace aht20_bmp280 {
namespace {

// ── Static state (no per-loop allocation) ────────────────────
Adafruit_AHTX0  aht;
Adafruit_BMP280 bmp(&Wire);
bool            ahtReady    = false;
bool            bmpReady    = false;
uint32_t        lastReadMs  = 0;
float           ahtTempC    = NAN;   // latest AHT20 temperature, °C (NAN until read)

constexpr uint32_t kReadIntervalMs = AHT20_BMP280_READ_INTERVAL_MS;  // ~2 Hz

constexpr float kPaToHpa = 1.0f / 100.0f;   // BMP280 reports Pa → hPa

}  // namespace

void begin() {
  // Probe both chips over the already-open Wire bus — do NOT call Wire.begin()
  // here; it is initialised once in setup() and shared with CH422G / GT911 /
  // MPU6050. Each chip is handled independently: one failing does not disable
  // the other, and neither is fatal to the rest of the board.

  // ── AHT20 (temperature + humidity, fixed address 0x38) ──
  ahtReady = aht.begin(&Wire, /*sensor_id=*/0, AHT20_I2C_ADDR);
  if (ahtReady)
    Serial.printf("[aht20_bmp280] AHT20 init OK  addr=0x%02X  (temp C, humidity %%RH)\n",
                  AHT20_I2C_ADDR);
  else
    Serial.printf("[aht20_bmp280] AHT20 not found at 0x%02X — check wiring (non-fatal)\n",
                  AHT20_I2C_ADDR);

  // ── BMP280 (temperature + pressure, 0x76 or 0x77) ──
  uint8_t bmpAddr = BMP280_I2C_ADDR;
  bmpReady = bmp.begin(BMP280_I2C_ADDR);
  if (!bmpReady) {                             // retry the alternate SDO-high address
    bmpAddr  = BMP280_I2C_ADDR_ALT;
    bmpReady = bmp.begin(BMP280_I2C_ADDR_ALT);
  }
  if (bmpReady) {
    // Adafruit's recommended indoor sampling: oversampling + IIR filter, 0.5 s standby.
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                    Adafruit_BMP280::SAMPLING_X2,    // temperature oversampling
                    Adafruit_BMP280::SAMPLING_X16,   // pressure oversampling
                    Adafruit_BMP280::FILTER_X16,
                    Adafruit_BMP280::STANDBY_MS_500);
    Serial.printf("[aht20_bmp280] BMP280 init OK  addr=0x%02X  (temp C, pressure hPa)\n",
                  bmpAddr);
  } else {
    Serial.printf("[aht20_bmp280] BMP280 not found at 0x%02X/0x%02X — check wiring (non-fatal)\n",
                  BMP280_I2C_ADDR, BMP280_I2C_ADDR_ALT);
  }
}

void update(uint32_t now_ms) {
  if (!ahtReady && !bmpReady) return;   // both chips absent — nothing to do

  if (now_ms - lastReadMs < kReadIntervalMs) return;
  lastReadMs = now_ms;

  // AHT20: cache the ambient temperature (°C) for the dashboard. Humidity is
  // returned in the same transaction but currently unused.
  if (ahtReady) {
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);
    ahtTempC = temp.temperature;
  }

  // BMP280: temperature (°C) + pressure (Pa → hPa).
  // if (bmpReady) {
  //   Serial.printf("[aht20_bmp280] BMP280 temp=%.2fC  pressure=%.2fhPa\n",
  //                 bmp.readTemperature(), bmp.readPressure() * kPaToHpa);
  // }
}

bool aht20Ready()  { return ahtReady; }
bool bmp280Ready() { return bmpReady; }

float ambientTemperatureC() { return ahtTempC; }

}  // namespace aht20_bmp280
