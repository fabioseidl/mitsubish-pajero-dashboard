#include "session_accumulator.h"

SessionAccumulator::SessionAccumulator()
    : total_distance_km_(0.0f), total_fuel_l_(0.0f) {}

void SessionAccumulator::update(float speed_km_h, float fuel_rate_l_per_h, uint32_t delta_ms) {
    if (speed_km_h > 0.0f) {
        total_distance_km_ += speed_km_h * ((float)delta_ms / 3600000.0f);
    }
    if (fuel_rate_l_per_h > 0.0f) {
        total_fuel_l_ += fuel_rate_l_per_h * ((float)delta_ms / 3600000.0f);
    }
}

float SessionAccumulator::getDistanceKm() const { return total_distance_km_; }
float SessionAccumulator::getTotalFuelL()  const { return total_fuel_l_; }

float SessionAccumulator::getAvgConsumptionKmPerL() const {
    if (total_fuel_l_ <= 0.0f) return 0.0f;
    return total_distance_km_ / total_fuel_l_;
}

void SessionAccumulator::reset() {
    total_distance_km_ = 0.0f;
    total_fuel_l_      = 0.0f;
}
