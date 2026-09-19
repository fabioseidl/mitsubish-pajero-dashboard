#pragma once

#include <stdint.h>

/// Running totals for the current trip: distance, fuel burned and elapsed
/// engine-on time. Fed at the broadcast rate and mirrored into RTC memory by the
/// server so a trip survives deep sleep.
class SessionAccumulator {
public:
    SessionAccumulator();

    /// Advance the totals by one broadcast tick. delta_ms always counts toward
    /// trip time; distance and fuel only accumulate while their rate is positive.
    void update(float speed_km_h, float fuel_rate_l_per_h, uint32_t delta_ms);

    /// Seed the totals from persisted state (see the RTC trip block in
    /// projects/server/src/main.cpp). Negative or non-finite inputs are treated as
    /// "no saved trip" and clamped to zero, so uninitialised RTC memory after a
    /// battery disconnect can never seed a garbage odometer.
    void restore(float distance_km, float total_fuel_l, uint32_t trip_time_s = 0);

    float getDistanceKm() const;
    float getTotalFuelL() const;
    float getAvgConsumptionKmPerL() const;
    /// Elapsed engine-on time this trip, in whole seconds.
    uint32_t getTripTimeS() const;
    void reset();

private:
    float    total_distance_km_;
    float    total_fuel_l_;
    // Kept in milliseconds so 100 ms ticks accumulate without rounding each one
    // away; wraps after 49 days of engine-on time, far beyond any single trip.
    uint32_t total_time_ms_;
};