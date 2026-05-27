/**
 * @file ESP_Panel_Board_Custom.h
 * Custom board configuration for the Waveshare ESP32-S3-Touch-LCD-7
 * (7-inch, 1024×600, ST7262 RGB controller, GT911 touch)
 *
 * This file is consumed by the ESP32_Display_Panel library when
 * -DESP_PANEL_USE_CUSTOM_BOARD=1 is set in build_flags.
 *
 * If the ESP32_Display_Panel library changes its configuration format,
 * regenerate from its lv_conf_template.h or use the library's GUI tool.
 *
 * Reference: https://github.com/esp-arduino-libs/ESP32_Display_Panel
 *            https://github.com/BitcoinManor/Waveshare-ESP32-S3-Touch-LCD-7-LVGL
 *
 * ⚠️  Verify ALL timing parameters against the ST7262 datasheet and the
 *     Waveshare schematic before running for the first time.
 */

#pragma once

// ============================================================================
// LCD Configuration
// ============================================================================

/**
 * Bus type: RGB parallel interface
 * 0: SPI, 1: QSPI, 2: RGB, 3: I80, 4: I2C
 */
#define ESP_PANEL_LCD_BUS_TYPE          (2)  /* RGB */

/**
 * Physical dimensions
 */
#define ESP_PANEL_LCD_H_RES             (1024)
#define ESP_PANEL_LCD_V_RES             (600)

/**
 * Color depth: 16-bit RGB565
 */
#define ESP_PANEL_LCD_COLOR_BITS        (16)

/**
 * LCD controller type.
 * Check ESP_Panel_LCD_Type.h in the library for the full list.
 */
#define ESP_PANEL_LCD_CONTROLLER        "ST7262"

// ---------------------------------------------------------------------------
// RGB Bus timing parameters for ST7262 / Waveshare 7" panel
// ⚠️  These are representative values; adjust from the panel datasheet or
//     by oscilloscope measurement if the image is shifted/corrupted.
// ---------------------------------------------------------------------------
#define ESP_PANEL_LCD_RGB_CLK_HZ        (18 * 1000 * 1000)  /* 18 MHz PCLK */
#define ESP_PANEL_LCD_RGB_HPW           (10)    /* HSYNC pulse width (pixels) */
#define ESP_PANEL_LCD_RGB_HBP           (160)   /* HSYNC back porch */
#define ESP_PANEL_LCD_RGB_HFP           (160)   /* HSYNC front porch */
#define ESP_PANEL_LCD_RGB_VPW           (3)     /* VSYNC pulse width (lines) */
#define ESP_PANEL_LCD_RGB_VBP           (23)    /* VSYNC back porch */
#define ESP_PANEL_LCD_RGB_VFP           (12)    /* VSYNC front porch */
#define ESP_PANEL_LCD_RGB_PCLK_ACTIVE_NEG (1)  /* Pixel clock active on falling edge */

// ---------------------------------------------------------------------------
// RGB data pins — match pin_config.h exactly
// ---------------------------------------------------------------------------
#define ESP_PANEL_LCD_RGB_IO_HSYNC      (46)
#define ESP_PANEL_LCD_RGB_IO_VSYNC      (3)
#define ESP_PANEL_LCD_RGB_IO_DE         (5)
#define ESP_PANEL_LCD_RGB_IO_PCLK       (7)

#define ESP_PANEL_LCD_RGB_IO_DATA0      (39)  /* G2  */
#define ESP_PANEL_LCD_RGB_IO_DATA1      (0)   /* G3  */
#define ESP_PANEL_LCD_RGB_IO_DATA2      (45)  /* G4  */
#define ESP_PANEL_LCD_RGB_IO_DATA3      (21)  /* G7  (G5/G6 wired on board) */
#define ESP_PANEL_LCD_RGB_IO_DATA4      (1)   /* R3  */
#define ESP_PANEL_LCD_RGB_IO_DATA5      (2)   /* R4  */
#define ESP_PANEL_LCD_RGB_IO_DATA6      (42)  /* R5  */
#define ESP_PANEL_LCD_RGB_IO_DATA7      (41)  /* R6  */
#define ESP_PANEL_LCD_RGB_IO_DATA8      (40)  /* R7  */
#define ESP_PANEL_LCD_RGB_IO_DATA9      (14)  /* B3  */
#define ESP_PANEL_LCD_RGB_IO_DATA10     (38)  /* B4  */
#define ESP_PANEL_LCD_RGB_IO_DATA11     (18)  /* B5  */
#define ESP_PANEL_LCD_RGB_IO_DATA12     (17)  /* B6  */
#define ESP_PANEL_LCD_RGB_IO_DATA13     (10)  /* B7  */
#define ESP_PANEL_LCD_RGB_IO_DATA14     (-1)  /* unused — verify */
#define ESP_PANEL_LCD_RGB_IO_DATA15     (-1)  /* unused — verify */

/**
 * LCD reset — managed through the I2C expander, NOT a direct GPIO.
 * Set to -1 so the library skips the GPIO reset pulse; reset is performed
 * manually via the expander in DebugScreen::initPanel().
 */
#define ESP_PANEL_LCD_IO_RST            (-1)

/**
 * Bounce buffer for the RGB peripheral.
 * The ESP32-S3 RGB LCD DMA and OPI PSRAM share the same bus; a bounce buffer
 * in internal SRAM prevents visual corruption when PSRAM is active.
 * Size in pixels: width × N lines. Start with 10 lines (1024 × 10 = 10 240 px).
 * Increase to 20 if you see corruption or screen tears.
 */
#define ESP_PANEL_LCD_RGB_BOUNCE_BUF_SIZE   (1024 * 10)

// ============================================================================
// Touch Configuration
// ============================================================================

/**
 * Enable touch controller
 */
#define ESP_PANEL_USE_TOUCH             (1)

/**
 * Touch controller type
 */
#define ESP_PANEL_TOUCH_CONTROLLER      "GT911"

/**
 * Touch bus: I2C
 */
#define ESP_PANEL_TOUCH_BUS_TYPE        (4)  /* I2C */

#define ESP_PANEL_TOUCH_I2C_ADDRESS     (0x5D)  /* GT911 default; 0x14 is alternate */
#define ESP_PANEL_TOUCH_I2C_CLK_HZ      (400 * 1000)  /* 400 kHz */

/**
 * Touch I2C pins — same I2C bus as the expander
 */
#define ESP_PANEL_TOUCH_I2C_IO_SCL      (9)
#define ESP_PANEL_TOUCH_I2C_IO_SDA      (8)

/**
 * Touch reset — managed through the expander; set to -1 here.
 * Manual reset is performed in DebugScreen::initTouch().
 */
#define ESP_PANEL_TOUCH_IO_RST          (-1)

/**
 * Touch interrupt pin — direct GPIO (not through expander)
 */
#define ESP_PANEL_TOUCH_IO_INT          (4)

/**
 * Touch panel dimensions — must match LCD
 */
#define ESP_PANEL_TOUCH_H_RES           (1024)
#define ESP_PANEL_TOUCH_V_RES           (600)

// ============================================================================
// Backlight Configuration
// ============================================================================

/**
 * Backlight is controlled via the I2C expander (IO_2), not a direct GPIO.
 * Disable the library's backlight driver by setting to -1.
 * DebugScreen::begin() drives backlight via s_expander->digitalWrite().
 */
#define ESP_PANEL_LCD_IO_BL             (-1)
#define ESP_PANEL_LCD_BL_ON_LEVEL       (1)
