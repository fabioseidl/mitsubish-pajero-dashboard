#pragma once

#define LGFX_USE_V1

#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

// All pin assignments and timing values are from BOARD_SPEC.md (verified working).
// Touch_GT911 is intentionally omitted here — LovyanGFX reinitializing I2C port 0
// inside lcd.init() conflicts with Arduino Wire (already owns port 0 for PCF8574).
class LGFX : public lgfx::LGFX_Device {
public:
    lgfx::Bus_RGB   _bus_instance;
    lgfx::Panel_RGB _panel_instance;

    LGFX() {
        // Panel
        auto cfg_panel = _panel_instance.config();
        cfg_panel.memory_width  = 1024;
        cfg_panel.memory_height = 600;
        cfg_panel.panel_width   = 1024;
        cfg_panel.panel_height  = 600;
        cfg_panel.offset_x = 0;
        cfg_panel.offset_y = 0;
        _panel_instance.config(cfg_panel);

        // Single framebuffer in PSRAM (use_psram=1) — REQUIRED here.
        // use_psram=2 (double buffer) works in the bare hello-world test, but in
        // the FULL app (LVGL draw buffers + WiFi/ESP-NOW heap) the two ~1.2 MB
        // framebuffers crash lcd.init() into a boot loop (RTC_SW_SYS_RST). The
        // original app ran fine on use_psram=1 and produces a valid image, so the
        // panel lights with a single buffer. Trade-off: mild horizontal tearing.
        auto cfg_detail = _panel_instance.config_detail();
        cfg_detail.use_psram = 1;
        _panel_instance.config_detail(cfg_detail);

        // RGB bus
        auto cfg = _bus_instance.config();
        cfg.panel = &_panel_instance;

        // BOARD_SPEC §3 verified "correct colors" mapping.
        // Blue: D0–D4
        cfg.pin_d0  = GPIO_NUM_14;
        cfg.pin_d1  = GPIO_NUM_38;
        cfg.pin_d2  = GPIO_NUM_18;
        cfg.pin_d3  = GPIO_NUM_17;
        cfg.pin_d4  = GPIO_NUM_10;

        // Green: D5–D10
        cfg.pin_d5  = GPIO_NUM_39;
        cfg.pin_d6  = GPIO_NUM_0;
        cfg.pin_d7  = GPIO_NUM_45;
        cfg.pin_d8  = GPIO_NUM_48;
        cfg.pin_d9  = GPIO_NUM_47;
        cfg.pin_d10 = GPIO_NUM_21;

        // Red: D11–D15
        cfg.pin_d11 = GPIO_NUM_1;
        cfg.pin_d12 = GPIO_NUM_2;
        cfg.pin_d13 = GPIO_NUM_42;
        cfg.pin_d14 = GPIO_NUM_41;
        cfg.pin_d15 = GPIO_NUM_40;

        // Control signals
        cfg.pin_henable = GPIO_NUM_5;
        cfg.pin_vsync   = GPIO_NUM_3;
        cfg.pin_hsync   = GPIO_NUM_46;
        cfg.pin_pclk    = GPIO_NUM_7;

        cfg.freq_write    = 16000000;
        cfg.pclk_idle_high = 0;

        cfg.hsync_polarity    = 0;
        cfg.hsync_front_porch = 40;
        cfg.hsync_pulse_width = 48;
        cfg.hsync_back_porch  = 88;

        cfg.vsync_polarity    = 0;
        cfg.vsync_front_porch = 3;
        cfg.vsync_pulse_width = 10;
        cfg.vsync_back_porch  = 18;

        _bus_instance.config(cfg);
        _panel_instance.setBus(&_bus_instance);

        setPanel(&_panel_instance);
    }
};
