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

    // Turbo boost (bar, GAUGE pressure — 0 = no boost).
    //
    // This vehicle has no dedicated boost PID: the 0xF300 DID the payload field was
    // originally wired to is one of the speculative Mode 22 IDs the ECU never
    // answers, so it read 0 forever. Boost is instead derived from two PIDs the ECU
    // does answer: absolute manifold pressure (0x0B) minus ambient pressure (0x33).
    static float computeBoostBar(const DataAggregator& aggregator);
};
