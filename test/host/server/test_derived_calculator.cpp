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

// ---- Overrun fuel cut-off ----

// Released pedal + elevated rpm + moving → no fuel, even with high air flow.
static void test_fuel_rate_overrun_returns_zero() {
    DataAggregator agg;
    agg.update(PID_MAF, 40.0f);       // air still flowing
    agg.update(PID_RPM, 2000.0f);
    agg.update(PID_SPEED, 80.0f);
    agg.update(PID_ACCEL_D, 0.0f);    // pedal released (valid reading)
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, DerivedCalculator::computeFuelRate(agg));
}

// Without any valid pedal signal the cut must NOT trigger (else an unsupported
// pedal PID would zero out fuel everywhere) — the MAF estimate stands.
static void test_fuel_rate_no_pedal_signal_does_not_cut() {
    DataAggregator agg;
    agg.update(PID_MAF, 40.0f);
    agg.update(PID_RPM, 2000.0f);
    agg.update(PID_SPEED, 80.0f);
    // no PID_ACCEL_D / PID_ACCEL_E / PID_THROTTLE updates → all invalid
    TEST_ASSERT_TRUE(DerivedCalculator::computeFuelRate(agg) > 0.0f);
}

// Pedal pressed → normal MAF-based estimate, not a cut.
static void test_fuel_rate_pedal_pressed_is_not_cut() {
    DataAggregator agg;
    agg.update(PID_MAF, 40.0f);
    agg.update(PID_RPM, 2000.0f);
    agg.update(PID_SPEED, 80.0f);
    agg.update(PID_ACCEL_D, 45.0f);   // pedal clearly pressed
    TEST_ASSERT_TRUE(DerivedCalculator::computeFuelRate(agg) > 0.0f);
}

// At idle (rpm below the overrun threshold) a released pedal is NOT engine
// braking, so the cut must not fire.
static void test_fuel_rate_idle_released_pedal_not_cut() {
    DataAggregator agg;
    agg.update(PID_MAF, 25.0f);
    agg.update(PID_RPM, 800.0f);      // idle, below FUEL_CUT_RPM_MIN
    agg.update(PID_SPEED, 0.0f);
    agg.update(PID_ACCEL_D, 0.0f);
    TEST_ASSERT_TRUE(DerivedCalculator::computeFuelRate(agg) > 0.0f);
}

void run_derived_calculator_tests() {
    RUN_TEST(test_fuel_rate_overrun_returns_zero);
    RUN_TEST(test_fuel_rate_no_pedal_signal_does_not_cut);
    RUN_TEST(test_fuel_rate_pedal_pressed_is_not_cut);
    RUN_TEST(test_fuel_rate_idle_released_pedal_not_cut);
    RUN_TEST(test_compute_normal_conditions);
    RUN_TEST(test_compute_zero_speed_returns_zero);
    RUN_TEST(test_compute_zero_fuel_rate_returns_zero);
    RUN_TEST(test_compute_both_zero_returns_zero);
    RUN_TEST(test_compute_negative_speed_returns_zero);
    RUN_TEST(test_compute_negative_fuel_rate_returns_zero);
    RUN_TEST(test_compute_low_speed_high_fuel_returns_low_value);
}

#include "../../../projects/server/src/derived_calculator.cpp"
