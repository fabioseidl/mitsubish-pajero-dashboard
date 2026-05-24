#include "derived_calculator.h"
#include "pid_map.h"

float DerivedCalculator::computeConsumption(const DataAggregator& aggregator) {
    float speed     = aggregator.get(PID_SPEED);
    float fuel_rate = aggregator.get(PID_FUEL_RATE);
    if (speed <= 0.0f || fuel_rate <= 0.0f) return 0.0f;
    return speed / fuel_rate;
}
