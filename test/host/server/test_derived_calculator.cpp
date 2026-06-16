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

// Released pedal + elevated rpm + moving + LOW load → true engine-braking, no
// fuel, even with high air flow.
static void test_fuel_rate_overrun_returns_zero() {
    DataAggregator agg;
    agg.update(PID_MAF, 40.0f);       // air still flowing
    agg.update(PID_RPM, 2000.0f);
    agg.update(PID_SPEED, 80.0f);
    agg.update(PID_ACCEL_D, 0.0f);    // pedal released (valid reading)
    agg.update(PID_ENGINE_LOAD, 5.0f); // fueling collapsed → real DFCO
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, DerivedCalculator::computeFuelRate(agg));
}

// Cruise control: physical pedal reads ~0 but the ECU is fueling to hold speed,
// so engine load stays high. The cut must NOT fire — this is the reported bug.
static void test_fuel_rate_cruise_control_not_cut() {
    DataAggregator agg;
    agg.update(PID_MAF, 60.0f);
    agg.update(PID_RPM, 2000.0f);
    agg.update(PID_SPEED, 95.0f);
    agg.update(PID_ACCEL_D, 0.0f);     // foot off the pedal (cruise holding)
    agg.update(PID_ENGINE_LOAD, 45.0f); // but the engine is loaded → fueling
    TEST_ASSERT_TRUE(DerivedCalculator::computeFuelRate(agg) > 0.0f);
}

// Released pedal + elevated rpm but the load PID is unavailable → cannot confirm
// overrun, so we must NOT cut (err toward over-reading, never zero a cruise).
static void test_fuel_rate_overrun_without_load_does_not_cut() {
    DataAggregator agg;
    agg.update(PID_MAF, 40.0f);
    agg.update(PID_RPM, 2000.0f);
    agg.update(PID_SPEED, 80.0f);
    agg.update(PID_ACCEL_D, 0.0f);     // pedal released, but no load reading
    TEST_ASSERT_TRUE(DerivedCalculator::computeFuelRate(agg) > 0.0f);
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

// ---- CAN 0x608 injected-fuel broadcast (preferred over MAF estimate) ----

// When the real injected-fuel broadcast is present, the rate is raw * rpm * K.
// (K = 4.1e-6, see FUEL_RAW_K in derived_calculator.cpp — keep in sync.)
static void test_fuel_rate_from_broadcast_scales_with_raw_and_rpm() {
    DataAggregator agg;
    agg.update(PID_BCAST_FUEL_RAW, 1000.0f);
    agg.update(PID_RPM, 2000.0f);
    // 1000 * 2000 * 4.1e-6 = 8.2 L/h
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.2f, DerivedCalculator::computeFuelRate(agg));
}

// raw == 0 is true deceleration fuel cut-off → exactly 0, even with air flowing
// and the pedal pressed (the broadcast is authoritative, no overrun heuristics).
static void test_fuel_rate_broadcast_zero_is_overrun() {
    DataAggregator agg;
    agg.update(PID_BCAST_FUEL_RAW, 0.0f);
    agg.update(PID_RPM, 2200.0f);
    agg.update(PID_MAF, 60.0f);          // air still flowing
    agg.update(PID_ACCEL_D, 30.0f);      // even a pressed pedal can't override it
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, DerivedCalculator::computeFuelRate(agg));
}

// The broadcast supersedes the MAF-based estimate: a high MAF would estimate a
// positive rate, but a zero broadcast wins.
static void test_fuel_rate_broadcast_supersedes_maf() {
    DataAggregator agg;
    agg.update(PID_MAF, 60.0f);
    agg.update(PID_RPM, 1500.0f);
    agg.update(PID_BCAST_FUEL_RAW, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, DerivedCalculator::computeFuelRate(agg));
}

// The broadcast also supersedes the direct PID 0x5E reading.
static void test_fuel_rate_broadcast_supersedes_direct_pid() {
    DataAggregator agg;
    agg.update(PID_FUEL_RATE, 50.0f);    // direct PID present
    agg.update(PID_BCAST_FUEL_RAW, 500.0f);
    agg.update(PID_RPM, 1000.0f);
    // 500 * 1000 * 4.1e-6 = 2.05 L/h (the broadcast), not 50
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.05f, DerivedCalculator::computeFuelRate(agg));
}

void run_derived_calculator_tests() {
    RUN_TEST(test_fuel_rate_from_broadcast_scales_with_raw_and_rpm);
    RUN_TEST(test_fuel_rate_broadcast_zero_is_overrun);
    RUN_TEST(test_fuel_rate_broadcast_supersedes_maf);
    RUN_TEST(test_fuel_rate_broadcast_supersedes_direct_pid);
    RUN_TEST(test_fuel_rate_overrun_returns_zero);
    RUN_TEST(test_fuel_rate_cruise_control_not_cut);
    RUN_TEST(test_fuel_rate_overrun_without_load_does_not_cut);
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
