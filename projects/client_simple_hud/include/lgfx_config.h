#pragma once

#ifndef UNIT_TEST
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "pin_config.h"

/**
 * LovyanGFX device class for CYD (ESP32-2432S028R).
 *
 * - Panel: ILI9341 240x320, SPI on VSPI (GPIOs in pin_config.h)
 * - Touch: XPT2046, same SPI bus
 * - Backlight: NOT managed here — CYDDisplay owns it via LEDC on GPIO 21
 */
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9341 _panel;
    lgfx::Bus_SPI       _bus;
    lgfx::Touch_XPT2046 _touch;

public:
    LGFX() {
        // SPI bus (display — HSPI)
        {
            auto cfg = _bus.config();
            cfg.spi_host    = HSPI_HOST;
            cfg.spi_mode    = 0;
            cfg.freq_write  = 27000000;
            cfg.freq_read   = 20000000;
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = GPIO_LCD_CLK;
            cfg.pin_mosi    = GPIO_LCD_MOSI;
            cfg.pin_miso    = GPIO_LCD_MISO;
            cfg.pin_dc      = GPIO_LCD_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }

        // Panel
        {
            auto cfg = _panel.config();
            cfg.pin_cs           = GPIO_LCD_CS;
            cfg.pin_rst          = GPIO_LCD_RST;
            cfg.pin_busy         = -1;
            cfg.memory_width     = 320;
            cfg.memory_height    = 240;
            cfg.panel_width      = 320;
            cfg.panel_height     = 240;
            cfg.offset_rotation  = 4;
            cfg.invert           = false;
            cfg.rgb_order        = true;
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = true;
            _panel.config(cfg);
        }

        // XPT2046 touch (VSPI — separate SPI bus)
        {
            auto cfg = _touch.config();
            cfg.x_min        = 0;
            cfg.x_max        = 319;
            cfg.y_min        = 0;
            cfg.y_max        = 239;
            cfg.pin_int      = GPIO_TOUCH_IRQ;
            cfg.bus_shared   = false;
            cfg.offset_rotation = 0;
            cfg.spi_host     = VSPI_HOST;
            cfg.freq         = 2500000;
            cfg.pin_sclk     = GPIO_TOUCH_SCLK;
            cfg.pin_mosi     = GPIO_TOUCH_MOSI;
            cfg.pin_miso     = GPIO_TOUCH_MISO;
            cfg.pin_cs       = GPIO_TOUCH_CS;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }

        setPanel(&_panel);
    }
};

#endif // UNIT_TEST
