#pragma once

#include <stdint.h>

class SessionAccumulator {
public:
    SessionAccumulator();

    void update(float speed_km_h, float fuel_rate_l_per_h, uint32_t delta_ms);
    float getDistanceKm() const;
    float getTotalFuelL() const;
    float getAvgConsumptionKmPerL() const;
    void reset();

private:
    float total_distance_km_;
    float total_fuel_l_;
};
