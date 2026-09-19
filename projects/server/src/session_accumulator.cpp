#include "session_accumulator.h"
#include <math.h>

SessionAccumulator::SessionAccumulator()
    : total_distance_km_(0.0f), total_fuel_l_(0.0f), total_time_ms_(0) {}

void SessionAccumulator::update(float speed_km_h, float fuel_rate_l_per_h, uint32_t delta_ms) {
    total_time_ms_ += delta_ms;
    if (speed_km_h > 0.0f) {
        total_distance_km_ += speed_km_h * ((float)delta_ms / 3600000.0f);
    }
    if (fuel_rate_l_per_h > 0.0f) {
        total_fuel_l_ += fuel_rate_l_per_h * ((float)delta_ms / 3600000.0f);
    }
}

void SessionAccumulator::restore(float distance_km, float total_fuel_l, uint32_t trip_time_s) {
    total_distance_km_ = (isfinite(distance_km)  && distance_km  > 0.0f) ? distance_km  : 0.0f;
    total_fuel_l_      = (isfinite(total_fuel_l) && total_fuel_l > 0.0f) ? total_fuel_l : 0.0f;
    total_time_ms_     = trip_time_s * 1000u;
}

float SessionAccumulator::getDistanceKm() const { return total_distance_km_; }
float SessionAccumulator::getTotalFuelL()  const { return total_fuel_l_; }

uint32_t SessionAccumulator::getTripTimeS() const { return total_time_ms_ / 1000u; }

float SessionAccumulator::getAvgConsumptionKmPerL() const {
    if (total_fuel_l_ <= 0.0f) return 0.0f;
    return total_distance_km_ / total_fuel_l_;
}

void SessionAccumulator::reset() {
    total_distance_km_ = 0.0f;
    total_fuel_l_      = 0.0f;
    total_time_ms_     = 0;
}
