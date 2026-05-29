#include "main_display.h"

#include <Arduino.h>
#include <Wire.h>

// ── DIAGNOSTIC TOGGLE ───────────────────────────────────────────────────────
// 1 = run the CH422G WR_IO(0x38) bit-scan forever in begin() (find the real
//     backlight EXIO bit). lcd.init() never runs in this mode.
// 0 = normal operation.
#define PCF_BITSCAN_DIAGNOSTIC 0

// ── I/O expander + backlight ─────────────────────────────────────────────────
// On this board the LCD backlight is NOT a normal ESP32 GPIO — it is an output
// pin of an I/O expander at I2C 0x24, which behaves as a plain PCF8574 (a single
// byte write latches all 8 output pins; 0x38 NACKs).
//
// The backlight lights from TWO things together:
//   1. the expander backlight pin armed HIGH before lcd.init() — we write 0xFF
//      to 0x24, which raises every pin (backlight + LCD reset + touch reset), and
//   2. lcd.init() starting continuous RGB DMA (see lgfx_config.h) — the panel's
//      LED driver only switches on once it sees a valid RGB signal.
// That is why the backlight visibly comes up at lcd.init(), not at the I2C write.
//
// The expander latch survives a warm reset (power stays on) but is cleared by a
// true power cut — so the firmware MUST re-arm 0xFF itself on every boot, which
// is exactly what makes cold boot work. Earlier code wrote only 0x07, leaving the
// real backlight bit unset; 0xFF asserts it regardless of which pin it is.
static constexpr uint8_t PCF8574_ADDR  = 0x24;  // I/O expander (PCF8574-compatible)
static constexpr int     I2C_SDA_PIN   = 8;
static constexpr int     I2C_SCL_PIN   = 9;

// CH422G register constants — used only by the optional bit-scan diagnostic
// below (PCF_BITSCAN_DIAGNOSTIC), kept in case a future board rev needs the
// CH422G output register (0x38) instead of the PCF8574-style 0x24 write.
static constexpr uint8_t CH422G_WR_SET = 0x24;  // CH422G system/mode register
static constexpr uint8_t CH422G_WR_IO  = 0x38;  // CH422G general-IO output register
static constexpr uint8_t CH422G_IO_OE  = 0x01;  // WR_SET: push-pull output enable

MainDisplay::MainDisplay(LGFX& lcd) : lcd_(lcd) {}

// Single-byte write to an explicit I2C address. Returns Wire.endTransmission():
// 0 = ACK/success, non-zero = error.
static uint8_t i2c_write_byte(uint8_t addr, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(val);
    return Wire.endTransmission();
}

// PCF8574-compatible port write (to 0x24). Kept for the PCF8574 fallback path.
static uint8_t pcf_write(uint8_t val) {
    return i2c_write_byte(PCF8574_ADDR, val);
}

// Arm the expander backlight/reset pins HIGH (panel out of reset, backlight pin
// HIGH). This is the EXACT verified-working write from the test project:
// 0xFF → 0x24. The backlight only physically lights once lcd.init() also starts
// continuous RGB DMA — see lgfx_config.h. We deliberately do NOT
// take a CH422G path here: on this board the chip behaves as a plain PCF8574 at
// 0x24 (0x38 NACKs), and writing 0xFF to a real CH422G mode register would set
// the SLEEP bit. 0xFF → 0x24 is the proven, safe recipe.
static uint8_t expander_drive_on() {
    return pcf_write(0xFF);
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
// CH422G output-register bit-scan. The previous code only ever wrote the MODE
// register (0x24), which configures the chip but does NOT drive pins — that is
// why the old scan never lit the backlight. Here we enable push-pull outputs via
// WR_SET (0x24 = 0x01), then drive each WR_IO (0x38) bit HIGH alone for 2 s.
//
// WATCH THE BACKLIGHT: whichever 2 s window lights it identifies the LCD_BL EXIO
// line (EXIO0 = first window, EXIO1 = second, …). Runs forever.
//
// Note: if EXIO5 = USB_SEL, the window that sets bit 5 (and the 0xFF window) may
// momentarily drop the USB serial log — keep watching the panel, not the log;
// the next loop sets bit 0 again and serial recovers.
static void ch422g_bitscan() {
    Serial.println("[bitscan] CH422G WR_IO(0x38) scan >>> WATCH THE BACKLIGHT, 2s/bit <<<");
    while (true) {
        // (Re)enable push-pull outputs each loop in case of glitches.
        i2c_write_byte(CH422G_WR_SET, CH422G_IO_OE);
        for (uint8_t b = 0; b < 8; ++b) {
            uint8_t v = (uint8_t)(1u << b);
            uint8_t r = i2c_write_byte(CH422G_WR_IO, v);
            Serial.printf("[bitscan] WR_SET=0x01  WR_IO=0x%02X  (EXIO%u HIGH)  ack=%u\n", v, b, r);
            delay(2000);
        }
        uint8_t r = i2c_write_byte(CH422G_WR_IO, 0xFF);
        Serial.printf("[bitscan] WR_SET=0x01  WR_IO=0xFF  (all EXIO HIGH) ack=%u\n", r);
        delay(2000);
        Serial.println("[bitscan] --- loop restart ---");
    }
}
#endif

bool MainDisplay::begin() {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000);   // match the verified-working test project (was 100kHz default)
    i2c_scan();
    initPCF8574();
    return true;
}

void MainDisplay::reapplyBacklight() {
    uint8_t r = expander_drive_on();
    // Throttled diagnostic: prove the steady-state backlight write keeps being
    // ACK'd after lcd.init() and WiFi come up. If this starts printing ack!=0,
    // the I2C bus is being disrupted by a later init step.
    static uint32_t last_log = 0;
    uint32_t now = millis();
    if (now - last_log >= 1000) {
        last_log = now;
        Serial.printf("[exp] reapply 0xFF->0x24 ack=%u\n", r);
    }
}

void MainDisplay::setBacklightPercent(uint8_t percent) {
    lcd_.setBrightness(static_cast<uint8_t>(percent * 255u / 100u));
}

void MainDisplay::initPCF8574() {
    // Verified-working PCF8574 sequence (matches the test project): pulse the
    // reset lines, then hold ALL pins HIGH (0xFF) so the backlight pin is armed
    // before lcd.init() starts the RGB DMA that actually lights the panel.
    uint8_t r;
    r = pcf_write(0x00); Serial.printf("[pcf] write 0x00 (all off)     ack=%u\n", r); delay(20);
    r = pcf_write(0x01); Serial.printf("[pcf] write 0x01 (LCD_RST)     ack=%u\n", r); delay(10);
    r = pcf_write(0x05); Serial.printf("[pcf] write 0x05 (RST+TP)      ack=%u\n", r); delay(50);
    r = pcf_write(0xFF); Serial.printf("[pcf] write 0xFF (all HIGH)    ack=%u\n", r);

    // Hold the pins high quietly before lcd.init() runs — give the backlight
    // converter time to soft-start before lcd.init()'s DMA begins.
    delay(300);

#if PCF_BITSCAN_DIAGNOSTIC
    ch422g_bitscan();   // never returns — the rest of the app does not start
#endif
}
