#pragma once

#include "data_aggregator.h"

class DerivedCalculator {
public:
    // Instantaneous consumption (km/L) from speed and fuel rate.
    static float computeConsumption(const DataAggregator& aggregator);

    // Fuel rate (L/h). Prefers the direct PID 0x5E reading; when the vehicle
    // does not support it (the Pajero diesel returns nothing), reports 0 during
    // overrun fuel cut-off (released pedal, elevated rpm, moving) and otherwise
    // estimates the rate from MAF air flow and a load-dependent diesel AFR.
    static float computeFuelRate(const DataAggregator& aggregator);

    // Altitude (m) above sea level from absolute barometric pressure (kPa),
    // using the international barometric formula.
    static float computeAltitude(float baro_kpa);
};
