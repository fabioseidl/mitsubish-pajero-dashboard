#include "gt911.h"

#include <Arduino.h>
#include <Wire.h>

namespace gt911 {
namespace {

// The GT911 uses 16-bit big-endian register addresses.
constexpr uint16_t REG_PRODUCT_ID = 0x8140;  // 4 bytes, reads "911\0"
constexpr uint16_t REG_STATUS     = 0x814E;  // bit7 = new data ready, bits0-3 = point count
constexpr uint16_t REG_POINT1     = 0x8150;  // 8 bytes: id, x_lo, x_hi, y_lo, y_hi, size_lo, size_hi, rsvd

// Address is decided by the INT level as reset is released. INT is not driven
// here, so which one answers depends on the board's pull — probe both.
constexpr uint8_t ADDR_PRIMARY = 0x5D;
constexpr uint8_t ADDR_ALT     = 0x14;

// Panel geometry, for clamping and for the optional axis fixes below.
constexpr uint16_t PANEL_W = 1024;
constexpr uint16_t PANEL_H = 600;

// Orientation fixes. The GT911 reports in its own frame, which need not match
// how the glass is bonded to this panel. Flip these if a touch lands mirrored
// or on the wrong axis — the raw values are logged on the first few touches to
// make that a two-minute job rather than a guess.
constexpr bool SWAP_XY  = false;
constexpr bool INVERT_X = false;
constexpr bool INVERT_Y = false;

uint8_t s_addr = 0;

bool read_regs(uint16_t reg, uint8_t* buf, size_t len) {
    Wire.beginTransmission(s_addr);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    // Repeated START (endTransmission(false)) — the GT911 drops the register
    // pointer if the bus goes idle between the write and the read.
    if (Wire.endTransmission(false) != 0) return false;

    if (Wire.requestFrom((int)s_addr, (int)len) != (int)len) return false;
    for (size_t i = 0; i < len && Wire.available(); ++i) buf[i] = Wire.read();
    return true;
}

bool write_reg(uint16_t reg, uint8_t value) {
    Wire.beginTransmission(s_addr);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool probe(uint8_t addr) {
    s_addr = addr;
    uint8_t id[4] = {0};
    if (!read_regs(REG_PRODUCT_ID, id, sizeof(id))) return false;
    // Guard against a phantom ACK: only "911" is this chip.
    return id[0] == '9' && id[1] == '1' && id[2] == '1';
}

}  // namespace

uint8_t address() { return s_addr; }

bool detect() {
    for (uint8_t addr : {ADDR_PRIMARY, ADDR_ALT}) {
        if (probe(addr)) return true;
    }
    s_addr = 0;
    return false;
}

bool begin() {
    if (detect()) {
        Serial.printf("[touch] GT911 found at 0x%02X\n", s_addr);
        return true;
    }
    Serial.println("[touch] GT911 NOT found at 0x5D or 0x14");
    return false;
}

bool read(uint16_t* x, uint16_t* y) {
    if (s_addr == 0) return false;

    // Latched between polls: the ready flag is only set when something changed,
    // so a finger held still would otherwise look like a release.
    static bool     touched = false;
    static uint16_t last_x = 0, last_y = 0;

    uint8_t status = 0;
    if (!read_regs(REG_STATUS, &status, 1)) return touched ? (*x = last_x, *y = last_y, true) : false;

    if (status & 0x80) {                     // new data
        const uint8_t points = status & 0x0F;
        if (points > 0) {
            uint8_t p[8] = {0};
            if (read_regs(REG_POINT1, p, sizeof(p))) {
                uint16_t rx = (uint16_t)p[1] | ((uint16_t)p[2] << 8);
                uint16_t ry = (uint16_t)p[3] | ((uint16_t)p[4] << 8);

                // Log the first few raw readings — the cheapest way to settle
                // the orientation constants above against the real glass.
                static uint8_t logged = 0;
                if (logged < 5) {
                    Serial.printf("[touch] raw x=%u y=%u (points=%u)\n", rx, ry, points);
                    ++logged;
                }

                if (SWAP_XY) { uint16_t t = rx; rx = ry; ry = t; }
                if (INVERT_X) rx = (PANEL_W - 1) - rx;
                if (INVERT_Y) ry = (PANEL_H - 1) - ry;

                if (rx >= PANEL_W) rx = PANEL_W - 1;
                if (ry >= PANEL_H) ry = PANEL_H - 1;

                last_x  = rx;
                last_y  = ry;
                touched = true;
            }
        } else {
            touched = false;
        }
        // Clearing the flag is mandatory: the GT911 stops reporting until the
        // host acknowledges the previous report.
        write_reg(REG_STATUS, 0);
    }

    *x = last_x;
    *y = last_y;
    return touched;
}

}  // namespace gt911
