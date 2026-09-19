#include "step_brightness.h"

StepBrightness::StepBrightness(IDisplay& display)
    : display_(display) {}

void StepBrightness::next() {
    level_ = (uint8_t)((level_ + 1) % LEVEL_COUNT);
    applyCurrent();
}

void StepBrightness::increase() {
    if (level_ + 1 < LEVEL_COUNT) {
        ++level_;
        applyCurrent();
    }
}

void StepBrightness::decrease() {
    if (level_ > 0) {
        --level_;
        applyCurrent();
    }
}

void StepBrightness::applyCurrent() {
    display_.setBacklightPercent(getCurrentPercent());
}
