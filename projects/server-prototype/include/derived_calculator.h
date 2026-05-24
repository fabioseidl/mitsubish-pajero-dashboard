#pragma once

#include "data_aggregator.h"

class DerivedCalculator {
public:
    static float computeConsumption(const DataAggregator& aggregator);
};
