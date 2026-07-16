#include "hud_brightness.h"

HudBrightness::HudBrightness(IDisplay& display)
    : display_(display) {}

void HudBrightness::next() {
    level_ = (uint8_t)((level_ + 1) % LEVEL_COUNT);
    applyCurrent();
}

void HudBrightness::applyCurrent() {
    display_.setBacklightPercent(getCurrentPercent());
}
