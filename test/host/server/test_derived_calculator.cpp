#include <unity.h>
#include "derived_calculator.h"
#include "data_aggregator.h"
#include "pid_map.h"

static void test_compute_normal_conditions() {
    DataAggregator agg;
    agg.update(PID_SPEED, 100.0f);
    agg.update(PID_FUEL_RATE, 5.0f);
    float result = DerivedCalculator::computeConsumption(agg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, result);
}

static void test_compute_zero_speed_returns_zero() {
    DataAggregator agg;
    agg.update(PID_SPEED, 0.0f);
    agg.update(PID_FUEL_RATE, 5.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, DerivedCalculator::computeConsumption(agg));
}

static void test_compute_zero_fuel_rate_returns_zero() {
    DataAggregator agg;
    agg.update(PID_SPEED, 100.0f);
    agg.update(PID_FUEL_RATE, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, DerivedCalculator::computeConsumption(agg));
}

static void test_compute_both_zero_returns_zero() {
    DataAggregator agg;
    agg.update(PID_SPEED, 0.0f);
    agg.update(PID_FUEL_RATE, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, DerivedCalculator::computeConsumption(agg));
}

static void test_compute_negative_speed_returns_zero() {
    DataAggregator agg;
    agg.update(PID_SPEED, -10.0f);
    agg.update(PID_FUEL_RATE, 5.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, DerivedCalculator::computeConsumption(agg));
}

static void test_compute_negative_fuel_rate_returns_zero() {
    DataAggregator agg;
    agg.update(PID_SPEED, 100.0f);
    agg.update(PID_FUEL_RATE, -1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, DerivedCalculator::computeConsumption(agg));
}

static void test_compute_low_speed_high_fuel_returns_low_value() {
    DataAggregator agg;
    agg.update(PID_SPEED, 10.0f);
    agg.update(PID_FUEL_RATE, 10.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, DerivedCalculator::computeConsumption(agg));
}

void run_derived_calculator_tests() {
    RUN_TEST(test_compute_normal_conditions);
    RUN_TEST(test_compute_zero_speed_returns_zero);
    RUN_TEST(test_compute_zero_fuel_rate_returns_zero);
    RUN_TEST(test_compute_both_zero_returns_zero);
    RUN_TEST(test_compute_negative_speed_returns_zero);
    RUN_TEST(test_compute_negative_fuel_rate_returns_zero);
    RUN_TEST(test_compute_low_speed_high_fuel_returns_low_value);
}

#include "../../../projects/server/src/derived_calculator.cpp"
