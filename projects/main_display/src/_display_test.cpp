#define LGFX_USE_V1

#include <Arduino.h>
#include <Wire.h>

#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

class LGFX : public lgfx::LGFX_Device {
public:
    lgfx::Bus_RGB   _bus_instance;
    lgfx::Panel_RGB _panel_instance;

    LGFX() {

        // =========================
        // Panel configuration
        // =========================

        auto cfg_panel = _panel_instance.config();

        cfg_panel.memory_width  = 1024;
        cfg_panel.memory_height = 600;
        cfg_panel.panel_width   = 1024;
        cfg_panel.panel_height  = 600;

        cfg_panel.offset_x = 0;
        cfg_panel.offset_y = 0;

        _panel_instance.config(cfg_panel);

        // =========================
        // PSRAM framebuffer
        // =========================

        auto cfg_detail = _panel_instance.config_detail();
        cfg_detail.use_psram = 2;
        _panel_instance.config_detail(cfg_detail);

        // =========================
        // RGB bus
        // =========================

        auto cfg = _bus_instance.config();

        cfg.panel = &_panel_instance;

        // BLUE
        cfg.pin_d0  = GPIO_NUM_14;
        cfg.pin_d1  = GPIO_NUM_38;
        cfg.pin_d2  = GPIO_NUM_18;
        cfg.pin_d3  = GPIO_NUM_17;
        cfg.pin_d4  = GPIO_NUM_10;

        // GREEN
        cfg.pin_d5  = GPIO_NUM_39;
        cfg.pin_d6  = GPIO_NUM_0;
        cfg.pin_d7  = GPIO_NUM_45;
        cfg.pin_d8  = GPIO_NUM_48;
        cfg.pin_d9  = GPIO_NUM_47;
        cfg.pin_d10 = GPIO_NUM_21;

        // RED
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

        // Pixel clock
        cfg.freq_write = 16000000;

        // Timing
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

LGFX lcd;

// =========================
// PCF8574 init
// =========================

void initIOExpander() {

    Wire.begin(8, 9);

    // All OFF / reset
    Wire.beginTransmission(0x24);
    Wire.write(0x00);
    Wire.endTransmission();
    delay(20);

    // LCD reset release
    Wire.beginTransmission(0x24);
    Wire.write(0x01);
    Wire.endTransmission();
    delay(10);

    // Touch reset release
    Wire.beginTransmission(0x24);
    Wire.write(0x05);
    Wire.endTransmission();
    delay(50);

    // Backlight ON
    Wire.beginTransmission(0x24);
    Wire.write(0x07);
    Wire.endTransmission();
}

// =========================
// Draw RGB test pattern
// =========================

void drawColorBars() {

    int w = lcd.width();
    int h = lcd.height();

    int barWidth = w / 6;

    lcd.fillRect(0 * barWidth, 0, barWidth, h, TFT_RED);
    lcd.fillRect(1 * barWidth, 0, barWidth, h, TFT_GREEN);
    lcd.fillRect(2 * barWidth, 0, barWidth, h, TFT_BLUE);

    lcd.fillRect(3 * barWidth, 0, barWidth, h, TFT_YELLOW);
    lcd.fillRect(4 * barWidth, 0, barWidth, h, TFT_CYAN);
    lcd.fillRect(5 * barWidth, 0, barWidth, h, TFT_MAGENTA);

    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextSize(3);

    lcd.drawString("RGB TEST", 20, 20);

    lcd.setTextSize(2);

    lcd.drawString("RED",      40, 100);
    lcd.drawString("GREEN",   200, 100);
    lcd.drawString("BLUE",    380, 100);
    lcd.drawString("YELLOW",  540, 100);
    lcd.drawString("CYAN",    720, 100);
    lcd.drawString("MAGENTA", 880, 100);
}

// =========================
// Arduino setup
// =========================

void setup() {

    Serial.begin(115200);

    delay(1000);

    Serial.println();
    Serial.println("Starting display test...");

    initIOExpander();

    lcd.init();

    lcd.setBrightness(255);

    drawColorBars();

    Serial.println("Display initialized.");
}

// =========================
// Main loop
// =========================

void loop() {

    static uint32_t last = 0;
    static bool invert = false;

    if (millis() - last > 3000) {

        last = millis();

        invert = !invert;

        lcd.invertDisplay(invert);

        Serial.printf("Invert: %s\n", invert ? "ON" : "OFF");
    }
}
