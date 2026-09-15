#pragma once
/**
 * Pin map for the Waveshare ESP32-C6-LCD-1.47 (onboard 1.47" ST7789, 172x320 IPS).
 *
 * The LCD shares its SPI bus with the onboard TF-card slot; the card is unused
 * and its CS is parked HIGH so it never drives MISO onto LCD traffic.
 */

// ── Display (SPI) ────────────────────────────────────────────────────────────
#define GPIO_LCD_MOSI   6
#define GPIO_LCD_SCLK   7
#define GPIO_LCD_CS     14
#define GPIO_LCD_DC     15
#define GPIO_LCD_RST    21

// ── Backlight ────────────────────────────────────────────────────────────────
// Active-HIGH, driven by LEDC PWM through CYDDisplay.
#define GPIO_BACKLIGHT_PIN 22

// ── TF card (unused, CS parked HIGH) ─────────────────────────────────────────
#define GPIO_SD_CS      4

// ── Panel geometry ───────────────────────────────────────────────────────────
// The ST7789 is a 240x320 controller; this 172-wide glass sits centred in that
// window, so column data starts at (240-172)/2 = 34.
#define PANEL_NATIVE_W  172
#define PANEL_NATIVE_H  320
#define PANEL_COL_OFFSET 34
#define PANEL_ROW_OFFSET 0

// Arduino_GFX rotation. 1 = landscape → LVGL sees SCREEN_W x SCREEN_H below.
#define SCREEN_ROTATION 1
#define SCREEN_W        320
#define SCREEN_H        172
