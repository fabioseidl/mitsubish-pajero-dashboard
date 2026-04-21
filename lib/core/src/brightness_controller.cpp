#include "brightness_controller.h"

constexpr uint8_t BrightnessController::LEVEL_PERCENTS[];

BrightnessController::BrightnessController(IDisplay& display)
    : display_(display), current_level_(2) {}

void BrightnessController::onTouch() {
    current_level_ = (current_level_ + 1) % LEVEL_COUNT;
    display_.setBacklightPercent(LEVEL_PERCENTS[current_level_]);
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
