#pragma once

#include <stdint.h>

class SessionAccumulator {
public:
    SessionAccumulator();

    void update(float speed_km_h, float fuel_rate_l_per_h, uint32_t delta_ms);

    // Seed the totals from persisted state (see the RTC trip block in
    // projects/server/src/main.cpp). Negative or non-finite inputs are treated as
    // "no saved trip" and clamped to zero, so uninitialised RTC memory after a
    // battery disconnect can never seed a garbage odometer.
    void restore(float distance_km, float total_fuel_l);

    float getDistanceKm() const;
    float getTotalFuelL() const;
    float getAvgConsumptionKmPerL() const;
    void reset();

private:
    float total_distance_km_;
    float total_fuel_l_;
};