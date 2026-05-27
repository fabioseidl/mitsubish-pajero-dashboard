#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// I2C bus — shared by IO expander (0x24) and GT911 touch controller
// ---------------------------------------------------------------------------
static constexpr int PIN_I2C_SDA = 8;
static constexpr int PIN_I2C_SCL = 9;

// ---------------------------------------------------------------------------
// I2C IO expander — PCF8574 at address 0x24
// Single-byte write protocol: one byte directly drives all 8 output pins.
// (Older board docs reference CH422G/TCA9554 register protocol — does NOT
//  apply to this board revision which has PCF8574.)
// ---------------------------------------------------------------------------
static constexpr uint8_t IO_EXP_ADDR      = 0x24;
static constexpr uint8_t IO_EXP_LCD_RST   = 0;  // P0 — LCD panel reset (HIGH = running)
static constexpr uint8_t IO_EXP_BACKLIGHT = 1;  // P1 — backlight enable (HIGH = ON)
static constexpr uint8_t IO_EXP_TOUCH_RST = 2;  // P2 — GT911 touch reset (HIGH = running)
static constexpr uint8_t IO_EXP_SD_CS     = 4;  // P4 — SD card CS (unused here)

// ---------------------------------------------------------------------------
// GT911 touch interrupt — direct GPIO (not through expander)
// Confirm against the Waveshare ESP32-S3-Touch-LCD-7 schematic.
// ---------------------------------------------------------------------------
static constexpr int PIN_TOUCH_INT = 4;

// ---------------------------------------------------------------------------
// RGB LCD parallel interface — ST7262 controller
// These GPIO assignments MUST match the hardware and must not be reassigned.
//
//  WARNING: GPIO0  (G3)     is also the ESP32-S3 BOOT/strapping pin.
//           Driving it LOW during power-on prevents download mode.
//           This is acceptable for a dedicated display MCU; be aware
//           when flashing during development.
//
//  WARNING: GPIO3  (VSYNC)  conflicts with JTAG_TDI.
//           Avoid JTAG debugging on this board.
//
//  WARNING: GPIO46 (HSYNC)  is used here as a typical ST7262 mapping.
//           Confirm with the actual board schematic — this pin may differ.
// ---------------------------------------------------------------------------
static constexpr int PIN_LCD_PCLK  = 7;
static constexpr int PIN_LCD_VSYNC = 3;
static constexpr int PIN_LCD_HSYNC = 46;  // ← verify against board schematic
static constexpr int PIN_LCD_DE    = 5;

// Red channel R[4:0]
static constexpr int PIN_LCD_R3 = 1;
static constexpr int PIN_LCD_R4 = 2;
static constexpr int PIN_LCD_R5 = 42;
static constexpr int PIN_LCD_R6 = 41;
static constexpr int PIN_LCD_R7 = 40;

// Green channel G[5:0]
// NOTE: G[5:0] in RGB565 = bits G5..G0.
// G5 (pin_d8) and G6 (pin_d9) are inferred from the Waveshare 4.3" sibling
// board (GPIO48/47).  Verify against the 7" board schematic before release.
// If confirmed unconnected, replace with any available spare GPIO — the pins
// must be valid numbers (not -1) for LovyanGFX's internal I80 GPIO-matrix
// configuration to succeed; they will toggle but their outputs go nowhere.
static constexpr int PIN_LCD_G2 = 39;
static constexpr int PIN_LCD_G3 = 0;
static constexpr int PIN_LCD_G4 = 45;
static constexpr int PIN_LCD_G5 = 48;  // ← verify against board schematic
static constexpr int PIN_LCD_G6 = 47;  // ← verify against board schematic
static constexpr int PIN_LCD_G7 = 21;

// Blue channel B[4:0]
static constexpr int PIN_LCD_B3 = 14;
static constexpr int PIN_LCD_B4 = 38;
static constexpr int PIN_LCD_B5 = 18;
static constexpr int PIN_LCD_B6 = 17;
static constexpr int PIN_LCD_B7 = 10;
