#pragma once

#include <stdint.h>

// Backlight (LEDC PWM)
static constexpr int GPIO_BACKLIGHT_PIN = 21;

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

// Secondary backlight enable (active HIGH)
static constexpr int GPIO_BL_EN = 27;
