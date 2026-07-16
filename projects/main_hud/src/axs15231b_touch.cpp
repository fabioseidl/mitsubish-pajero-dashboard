#include "axs15231b_touch.h"

#ifndef UNIT_TEST

#include <Arduino.h>
#include <Wire.h>

#include "pin_config.h"

// Fixed command that asks the controller for a single-finger position report.
// The trailing 0x08 is the number of bytes to return.
static const uint8_t READ_TOUCHPAD_CMD[8] = {0xB5, 0xAB, 0xA5, 0x5A, 0x00, 0x00, 0x00, 0x08};

// Report layout: [0]=gesture [1]=finger count [2..3]=X (12-bit) [4..5]=Y (12-bit)
static constexpr size_t REPORT_LEN = 8;

AXS15231BTouch* AXS15231BTouch::instance_  = nullptr;
volatile bool   AXS15231BTouch::int_fired_ = false;

AXS15231BTouch::AXS15231BTouch(uint8_t scl, uint8_t sda, uint8_t int_pin, uint8_t addr)
    : scl_(scl), sda_(sda), int_pin_(int_pin), addr_(addr) {}

void IRAM_ATTR AXS15231BTouch::onInterrupt() {
    int_fired_ = true;
}

bool AXS15231BTouch::begin() {
    instance_ = this;

    pinMode(int_pin_, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(int_pin_), onInterrupt, FALLING);

    if (!Wire.begin(sda_, scl_)) return false;
    Wire.setClock(400000);
    return true;
}

bool AXS15231BTouch::read(uint16_t* x, uint16_t* y) {
    if (!int_fired_) return false;
    int_fired_ = false;

    Wire.beginTransmission(addr_);
    Wire.write(READ_TOUCHPAD_CMD, sizeof(READ_TOUCHPAD_CMD));
    if (Wire.endTransmission() != 0) return false;

    uint8_t buf[REPORT_LEN] = {0};
    if (Wire.requestFrom(addr_, (uint8_t)REPORT_LEN) != REPORT_LEN) return false;
    for (size_t i = 0; i < REPORT_LEN && Wire.available(); ++i) {
        buf[i] = Wire.read();
    }

    // buf[1] = fingers down. The controller interrupts on finger-LIFT as well as
    // on touch, and that report carries zero fingers and stale coordinates.
    // Reporting it as a touch fed LVGL a bogus point mid-press, which LVGL reads
    // as the pointer leaving the button — and a pointer that leaves cancels the
    // click. That is what made the buttons feel locked and need several presses.
    const uint8_t fingers = buf[1] & 0x0F;
    if (fingers == 0) return false;

    // 12-bit coordinates: low nibble of the first byte is the high 4 bits.
    uint16_t raw_x = ((uint16_t)(buf[2] & 0x0F) << 8) | buf[3];
    uint16_t raw_y = ((uint16_t)(buf[4] & 0x0F) << 8) | buf[5];

    // Clamp into the calibrated window, then stretch that window over the panel.
    raw_x = constrain(raw_x, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX);
    raw_y = constrain(raw_y, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX);
    uint16_t cal_x = map(raw_x, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX, 0, PANEL_NATIVE_W - 1);
    uint16_t cal_y = map(raw_y, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX, 0, PANEL_NATIVE_H - 1);

    // Portrait (native) → landscape, matching SCREEN_ROTATION = 1.
#if SCREEN_ROTATION == 1
    *x = cal_y;
    *y = (PANEL_NATIVE_W - 1) - cal_x;
#elif SCREEN_ROTATION == 3
    *x = (PANEL_NATIVE_H - 1) - cal_y;
    *y = cal_x;
#elif SCREEN_ROTATION == 2
    *x = (PANEL_NATIVE_W - 1) - cal_x;
    *y = (PANEL_NATIVE_H - 1) - cal_y;
#else
    *x = cal_x;
    *y = cal_y;
#endif
    return true;
}

#endif // UNIT_TEST
