#include "main_display.h"

#include <Arduino.h>
#include <Wire.h>

// ── DIAGNOSTIC TOGGLE ───────────────────────────────────────────────────────
// 1 = run the PCF8574 bit-scan forever in begin() (find the real backlight bit).
// 0 = normal operation.
#define PCF_BITSCAN_DIAGNOSTIC 0

static constexpr uint8_t PCF8574_ADDR = 0x24;
static constexpr int     I2C_SDA_PIN  = 8;
static constexpr int     I2C_SCL_PIN  = 9;

MainDisplay::MainDisplay(LGFX& lcd) : lcd_(lcd) {}

// PCF8574 single-byte port write. Returns Wire.endTransmission():
// 0 = ACK/success, non-zero = error.
static uint8_t pcf_write(uint8_t val) {
    Wire.beginTransmission(PCF8574_ADDR);
    Wire.write(val);
    return Wire.endTransmission();
}

static void i2c_scan() {
    Serial.println("[i2c] scanning bus...");
    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 127; ++addr) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[i2c] found device 0x%02X\n", addr);
            ++found;
        }
    }
    Serial.printf("[i2c] scan done, %u device(s)\n", found);
}

#if PCF_BITSCAN_DIAGNOSTIC
// Writes every PCF8574 value for 2.5 s each, forever. WATCH THE BACKLIGHT and
// note which value(s) light it. 0xFF drives ALL pins high — if even 0xFF does
// not light the backlight, it is not controlled by this PCF8574 (power / wiring).
static void pcf_bitscan() {
    static const uint8_t patterns[] = {
        0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0xFF
    };
    Serial.println("[bitscan] >>> WATCH THE BACKLIGHT — 2.5s per value <<<");
    while (true) {
        for (size_t i = 0; i < sizeof(patterns); ++i) {
            uint8_t v = patterns[i];
            uint8_t r = pcf_write(v);
            Serial.printf("[bitscan] PCF=0x%02X  (P0=%d P1=%d P2=%d P3=%d P4=%d P5=%d P6=%d P7=%d)  ack=%u\n",
                          v,
                          (v >> 0) & 1, (v >> 1) & 1, (v >> 2) & 1, (v >> 3) & 1,
                          (v >> 4) & 1, (v >> 5) & 1, (v >> 6) & 1, (v >> 7) & 1,
                          r);
            delay(2500);
        }
        Serial.println("[bitscan] --- loop restart ---");
    }
}
#endif

bool MainDisplay::begin() {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    i2c_scan();
    initPCF8574();
    return true;
}

void MainDisplay::reapplyBacklight() {
    uint8_t r = pcf_write(0x07);
    // Throttled diagnostic: prove the steady-state backlight write keeps being
    // ACK'd after lcd.init() and WiFi come up. If this starts printing ack!=0,
    // the I2C bus is being disrupted by a later init step.
    static uint32_t last_log = 0;
    uint32_t now = millis();
    if (now - last_log >= 1000) {
        last_log = now;
        Serial.printf("[pcf] reapply 0x07 ack=%u\n", r);
    }
}

void MainDisplay::setBacklightPercent(uint8_t percent) {
    lcd_.setBrightness(static_cast<uint8_t>(percent * 255u / 100u));
}

void MainDisplay::initPCF8574() {
    uint8_t r;
    r = pcf_write(0x00); Serial.printf("[pcf] write 0x00 (all off)     ack=%u\n", r); delay(20);
    r = pcf_write(0x01); Serial.printf("[pcf] write 0x01 (LCD_RST)     ack=%u\n", r); delay(10);
    r = pcf_write(0x05); Serial.printf("[pcf] write 0x05 (RST+TP)      ack=%u\n", r); delay(50);
    r = pcf_write(0x07); Serial.printf("[pcf] write 0x07 (RST+BL+TP)   ack=%u\n", r);

#if PCF_BITSCAN_DIAGNOSTIC
    pcf_bitscan();   // never returns — the rest of the app does not start
#endif
}
