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

// The broadcast is a fuel MASS FLOW in mg/s, so the rate is rpm-independent:
//   L/h = raw * 3.6 / DIESEL_DENSITY_G_L   (835 g/L — keep in sync with
//   derived_calculator.cpp).
static void test_fuel_rate_from_broadcast_scales_with_raw() {
    DataAggregator agg;
    agg.update(PID_BCAST_FUEL_RAW, 1000.0f);
    agg.update(PID_RPM, 2000.0f);
    // 1000 * 3.6 / 835 = 4.3114 L/h
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.3114f, DerivedCalculator::computeFuelRate(agg));
}

// Regression guard for the rpm-multiplied model that used to live here: it read
// far too low at idle and predicted a physically impossible ~74 L/h under load
// (a 4M41 cannot exceed ~32 L/h). Same raw at two very different rpm MUST give
// the same rate — if this fails, an rpm term has crept back into the formula.
static void test_fuel_rate_from_broadcast_is_rpm_independent() {
    DataAggregator low, high;
    low.update(PID_BCAST_FUEL_RAW, 1500.0f);
    low.update(PID_RPM, 800.0f);
    high.update(PID_BCAST_FUEL_RAW, 1500.0f);
    high.update(PID_RPM, 3600.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, DerivedCalculator::computeFuelRate(low),
                                     DerivedCalculator::computeFuelRate(high));
}

// The decoded idle figure must be physically sane for a warm 3.2 DI-D: our own
// FUELLOG idle sample (raw≈318) should land near ~1.4 L/h, not the ~0.85 L/h the
// old model produced.
static void test_fuel_rate_broadcast_idle_is_physical() {
    DataAggregator agg;
    agg.update(PID_BCAST_FUEL_RAW, 318.0f);
    agg.update(PID_RPM, 650.0f);
    float l_per_h = DerivedCalculator::computeFuelRate(agg);
    TEST_ASSERT_TRUE(l_per_h > 1.0f && l_per_h < 1.8f);
}

// ...and the hard-accel sample must stay under the engine's absolute fuel
// ceiling (~32 L/h at peak power, 123 kW @ ~215 g/kWh).
static void test_fuel_rate_broadcast_full_load_under_engine_ceiling() {
    DataAggregator agg;
    agg.update(PID_BCAST_FUEL_RAW, 4734.0f);
    agg.update(PID_RPM, 3800.0f);
    TEST_ASSERT_TRUE(DerivedCalculator::computeFuelRate(agg) < 32.0f);
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
    // 500 * 3.6 / 835 = 2.1557 L/h (the broadcast), not 50
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.1557f, DerivedCalculator::computeFuelRate(agg));
}

// ---- Boost pressure (derived: MAP - ambient) ----

// Regression guard for the reported bug: the payload's boost field used to read
// PID_M22_BOOST_PRES (a 0xF3xx DID this ECU never answers), so the dash showed 0
// forever. With real manifold + ambient readings it must now be non-zero.
static void test_boost_from_map_minus_baro() {
    DataAggregator agg;
    agg.update(PID_MAP_PRESSURE, 180.0f);   // kPa absolute
    agg.update(PID_BARO_PRESSURE, 100.0f);  // kPa ambient
    // (180 - 100) / 100 = 0.8 bar gauge
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.8f, DerivedCalculator::computeBoostBar(agg));
}

// Off boost the manifold sits at roughly ambient → 0, not a negative reading.
static void test_boost_off_boost_is_zero() {
    DataAggregator agg;
    agg.update(PID_MAP_PRESSURE, 100.0f);
    agg.update(PID_BARO_PRESSURE, 100.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, DerivedCalculator::computeBoostBar(agg));
}

// Slight vacuum (intake restriction / whole-kPa rounding) clamps to 0 rather than
// showing a negative value on a boost gauge.
static void test_boost_negative_clamps_to_zero() {
    DataAggregator agg;
    agg.update(PID_MAP_PRESSURE, 96.0f);
    agg.update(PID_BARO_PRESSURE, 101.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, DerivedCalculator::computeBoostBar(agg));
}

// Ambient PID unsupported → fall back to ISA sea level rather than a dead gauge.
static void test_boost_without_baro_uses_sea_level_fallback() {
    DataAggregator agg;
    agg.update(PID_MAP_PRESSURE, 201.3f);
    TEST_ASSERT_FALSE(agg.isValid(PID_BARO_PRESSURE));
    // (201.3 - 101.3) / 100 = 1.0 bar
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, DerivedCalculator::computeBoostBar(agg));
}

// No manifold reading at all → nothing to derive, report 0 (not a fallback-driven
// negative or a bogus positive).
static void test_boost_without_map_is_zero() {
    DataAggregator agg;
    agg.update(PID_BARO_PRESSURE, 100.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, DerivedCalculator::computeBoostBar(agg));
}

void run_derived_calculator_tests() {
    RUN_TEST(test_boost_from_map_minus_baro);
    RUN_TEST(test_boost_off_boost_is_zero);
    RUN_TEST(test_boost_negative_clamps_to_zero);
    RUN_TEST(test_boost_without_baro_uses_sea_level_fallback);
    RUN_TEST(test_boost_without_map_is_zero);
    RUN_TEST(test_fuel_rate_from_broadcast_scales_with_raw);
    RUN_TEST(test_fuel_rate_from_broadcast_is_rpm_independent);
    RUN_TEST(test_fuel_rate_broadcast_idle_is_physical);
    RUN_TEST(test_fuel_rate_broadcast_full_load_under_engine_ceiling);
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
