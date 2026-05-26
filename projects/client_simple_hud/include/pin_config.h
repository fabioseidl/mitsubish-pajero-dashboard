#pragma once

#include <stdint.h>

// Backlight PWM (LEDC) — confirmed by board definition: DISPLAY_BCKL=27.
// This is the only backlight control pin; there is no secondary enable.
static constexpr int GPIO_BACKLIGHT_PIN = 27;

// LDR light sensor (ADC input-only)
static constexpr int GPIO_LDR_PIN = 34;

// ILI9341 display SPI (VSPI / SPI2)
static constexpr int GPIO_LCD_MOSI = 13;
static constexpr int GPIO_LCD_CLK  = 14;
static constexpr int GPIO_LCD_CS   = 15;
static constexpr int GPIO_LCD_DC   = 2;
static constexpr int GPIO_LCD_RST  = -1;
static constexpr int GPIO_LCD_MISO = 12;

// XPT2046 touch (VSPI — separate SPI bus from display)
static constexpr int GPIO_TOUCH_CS   = 33;
static constexpr int GPIO_TOUCH_IRQ  = 36;
static constexpr int GPIO_TOUCH_SCLK = 25;
static constexpr int GPIO_TOUCH_MOSI = 32;
static constexpr int GPIO_TOUCH_MISO = 39;

// Capacitive touch controller interrupt (CST816S, I2C).
// This is an INPUT — do NOT drive it as an output.
static constexpr int GPIO_TOUCH_INT = 21;

// Built-in BOOT button on top of CYD board (active LOW, internal pull-up)
static constexpr int GPIO_BUTTON_PIN = 0;
