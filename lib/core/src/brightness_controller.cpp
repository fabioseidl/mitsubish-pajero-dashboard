#include "brightness_controller.h"

constexpr uint8_t BrightnessController::LEVEL_PERCENTS[];

BrightnessController::BrightnessController(IDisplay& display)
    : display_(display),
      current_level_(2),
      ldr_ema_(0.0f), // start assuming dark → full brightness until first real reading
      ldr_percent_(0) {}

void BrightnessController::onTouch(uint32_t now_ms) {
    current_level_ = (current_level_ + 1) % LEVEL_COUNT;
    display_.setBacklightPercent(LEVEL_PERCENTS[current_level_]);
    manual_override_until_ms_ = now_ms + MANUAL_OVERRIDE_MS;
}

uint8_t BrightnessController::getCurrentLevel() const {
    return current_level_;
}

uint8_t BrightnessController::getCurrentPercent() const {
    return LEVEL_PERCENTS[current_level_];
}

void BrightnessController::applyInitial() {
    display_.setBacklightPercent(LEVEL_PERCENTS[current_level_]);
}

void BrightnessController::onLdrReading(uint16_t raw_adc, uint32_t now_ms) {
    // Respect manual override: if the user adjusted brightness recently, don't
    // override their choice until the hold-off window expires.
    if (now_ms < manual_override_until_ms_) return;
    // Update exponential moving average
    ldr_ema_ = LDR_EMA_ALPHA * raw_adc + (1.0f - LDR_EMA_ALPHA) * ldr_ema_;

    // Clamp to the calibrated sensor range
    float v = ldr_ema_;
    if (v < LDR_ADC_MIN) v = LDR_ADC_MIN;
    if (v > LDR_ADC_MAX) v = LDR_ADC_MAX;

    // Normalise to [0.0, 1.0]
    float norm = (v - LDR_ADC_MIN) / (float)(LDR_ADC_MAX - LDR_ADC_MIN);

    // Invert if the circuit wires bright-light to a low ADC reading
    if (LDR_INVERT) norm = 1.0f - norm;

    // Map to backlight percent [BL_PCT_MIN, BL_PCT_MAX]
    ldr_percent_ = (uint8_t)(BL_PCT_MIN + norm * (BL_PCT_MAX - BL_PCT_MIN));

    display_.setBacklightPercent(ldr_percent_);
}

uint8_t BrightnessController::getLdrPercent() const {
    return ldr_percent_;
}
