#pragma once

/**
 * Pin map for the Guition JC3248W535 (ESP32-S3-N16R8, 3.5" 320x480 IPS).
 *
 * Panel and touch are both driven by an AXS15231B: the display side hangs off a
 * QSPI bus, the touch side off its own I2C bus. There is no DC pin — QSPI encodes
 * command vs. data in the transfer itself. Note GPIO8 is the touch I2C clock;
 * some board pinouts online label it "LCD DC", which is wrong for QSPI mode.
 */

// ── Display (QSPI) ───────────────────────────────────────────────────────────
#define GPIO_LCD_CS     45
#define GPIO_LCD_SCK    47
#define GPIO_LCD_D0     21
#define GPIO_LCD_D1     48
#define GPIO_LCD_D2     40
#define GPIO_LCD_D3     39

// ── Backlight ────────────────────────────────────────────────────────────────
// Active-HIGH, driven by LEDC PWM from HudDisplay.
#define GPIO_BACKLIGHT_PIN 1

// ── Touch (I2C) ──────────────────────────────────────────────────────────────
#define GPIO_TOUCH_SDA  4
#define GPIO_TOUCH_SCL  8
#define GPIO_TOUCH_INT  3
#define TOUCH_I2C_ADDR  0x3B

// ── Panel geometry ───────────────────────────────────────────────────────────
// The glass is natively portrait; the UI runs landscape (see SCREEN_ROTATION).
#define PANEL_NATIVE_W  320
#define PANEL_NATIVE_H  480

// Arduino_GFX rotation applied when the canvas is pushed to the panel.
// 1 = landscape → LVGL sees SCREEN_W x SCREEN_H below.
#define SCREEN_ROTATION 1
#define SCREEN_W        480
#define SCREEN_H        320

// ── Touch calibration ────────────────────────────────────────────────────────
// Raw controller range, measured at rotation 0. Mapped onto the full panel.
#define TOUCH_RAW_X_MIN 12
#define TOUCH_RAW_X_MAX 310
#define TOUCH_RAW_Y_MIN 14
#define TOUCH_RAW_Y_MAX 461
