#include <unity.h>
#include <math.h>
#include "session_accumulator.h"

static void test_initial_distance_is_zero() {
    SessionAccumulator s;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.getDistanceKm());
}

static void test_initial_fuel_is_zero() {
    SessionAccumulator s;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.getTotalFuelL());
}

static void test_initial_avg_consumption_is_zero() {
    SessionAccumulator s;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.getAvgConsumptionKmPerL());
}

static void test_initial_trip_time_is_zero() {
    SessionAccumulator s;
    TEST_ASSERT_EQUAL_UINT32(0, s.getTripTimeS());
}

static void test_update_accumulates_distance_correctly() {
    SessionAccumulator s;
    s.update(100.0f, 0.0f, 3600000);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, s.getDistanceKm());
}

static void test_update_accumulates_fuel_correctly() {
    SessionAccumulator s;
    s.update(0.0f, 8.0f, 3600000);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 8.0f, s.getTotalFuelL());
}

static void test_avg_consumption_computed_correctly() {
    SessionAccumulator s;
    s.update(100.0f, 8.0f, 3600000);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.5f, s.getAvgConsumptionKmPerL());
}

static void test_update_small_delta_accumulates_correctly() {
    SessionAccumulator s;
    s.update(60.0f, 0.0f, 100);
    float expected = 60.0f * (100.0f / 3600000.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, expected, s.getDistanceKm());
}

static void test_multiple_updates_accumulate_correctly() {
    SessionAccumulator s;
    for (int i = 0; i < 3600; i++) {
        s.update(60.0f, 6.0f, 1000);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 60.0f, s.getDistanceKm());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.0f, s.getTotalFuelL());
}

// Trip time counts wall time while the server is broadcasting, so it keeps
// running at a standstill — unlike distance and fuel, which are rate-gated.
static void test_trip_time_accumulates_while_stopped() {
    SessionAccumulator s;
    s.update(0.0f, 0.0f, 30000);
    TEST_ASSERT_EQUAL_UINT32(30, s.getTripTimeS());
}

static void test_trip_time_accumulates_sub_second_ticks() {
    SessionAccumulator s;
    for (int i = 0; i < 25; i++) {
        s.update(0.0f, 0.0f, 100);             // 25 broadcast ticks = 2.5 s
    }
    TEST_ASSERT_EQUAL_UINT32(2, s.getTripTimeS());
}

static void test_zero_speed_does_not_accumulate_distance() {
    SessionAccumulator s;
    s.update(0.0f, 0.5f, 100000);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.getDistanceKm());
}

static void test_zero_fuel_does_not_accumulate_fuel() {
    SessionAccumulator s;
    s.update(50.0f, 0.0f, 100000);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.getTotalFuelL());
}

static void test_negative_speed_treated_as_zero() {
    SessionAccumulator s;
    s.update(-10.0f, 2.0f, 100000);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.getDistanceKm());
}

static void test_reset_clears_all_accumulators() {
    SessionAccumulator s;
    s.update(100.0f, 8.0f, 3600000);
    s.reset();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.getDistanceKm());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.getTotalFuelL());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.getAvgConsumptionKmPerL());
    TEST_ASSERT_EQUAL_UINT32(0, s.getTripTimeS());
}

// --- restore() (trip persistence across deep sleep) ------------------------
// The server keeps the running totals in RTC memory so an ignition-off no longer
// wipes the trip; restore() seeds them back on wake. RTC memory is garbage after
// a battery disconnect, so restore() must refuse to seed nonsense.

static void test_restore_seeds_totals() {
    SessionAccumulator s;
    s.restore(120.0f, 10.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 120.0f, s.getDistanceKm());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f,  s.getTotalFuelL());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.0f,  s.getAvgConsumptionKmPerL());
}

static void test_restore_seeds_trip_time() {
    SessionAccumulator s;
    s.restore(120.0f, 10.0f, 3600);
    TEST_ASSERT_EQUAL_UINT32(3600, s.getTripTimeS());
}

static void test_restore_then_update_continues_trip_time() {
    SessionAccumulator s;
    s.restore(0.0f, 0.0f, 600);
    s.update(0.0f, 0.0f, 60000);
    TEST_ASSERT_EQUAL_UINT32(660, s.getTripTimeS());
}

static void test_restore_without_trip_time_starts_at_zero() {
    SessionAccumulator s;
    s.restore(120.0f, 10.0f);
    TEST_ASSERT_EQUAL_UINT32(0, s.getTripTimeS());
}

static void test_restore_then_update_continues_from_seed() {
    SessionAccumulator s;
    s.restore(100.0f, 10.0f);
    s.update(100.0f, 10.0f, 3600000);          // +100 km, +10 L over one hour
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 200.0f, s.getDistanceKm());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f,  s.getTotalFuelL());
}

static void test_restore_rejects_negative_values() {
    SessionAccumulator s;
    s.restore(-5.0f, -1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.getDistanceKm());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.getTotalFuelL());
}

static void test_restore_rejects_nan_from_uninitialised_rtc_memory() {
    SessionAccumulator s;
    s.restore(NAN, NAN);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.getDistanceKm());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.getTotalFuelL());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.getAvgConsumptionKmPerL());
}

static void test_restore_rejects_infinity() {
    SessionAccumulator s;
    s.restore(INFINITY, INFINITY);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.getDistanceKm());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.getTotalFuelL());
}

static void test_restore_zero_is_a_clean_trip() {
    SessionAccumulator s;
    s.restore(0.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.getDistanceKm());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.getAvgConsumptionKmPerL());
}

void run_session_accumulator_tests() {
    RUN_TEST(test_initial_distance_is_zero);
    RUN_TEST(test_initial_fuel_is_zero);
    RUN_TEST(test_initial_avg_consumption_is_zero);
    RUN_TEST(test_initial_trip_time_is_zero);
    RUN_TEST(test_update_accumulates_distance_correctly);
    RUN_TEST(test_update_accumulates_fuel_correctly);
    RUN_TEST(test_avg_consumption_computed_correctly);
    RUN_TEST(test_update_small_delta_accumulates_correctly);
    RUN_TEST(test_multiple_updates_accumulate_correctly);
    RUN_TEST(test_trip_time_accumulates_while_stopped);
    RUN_TEST(test_trip_time_accumulates_sub_second_ticks);
    RUN_TEST(test_zero_speed_does_not_accumulate_distance);
    RUN_TEST(test_zero_fuel_does_not_accumulate_fuel);
    RUN_TEST(test_negative_speed_treated_as_zero);
    RUN_TEST(test_reset_clears_all_accumulators);
    RUN_TEST(test_restore_seeds_totals);
    RUN_TEST(test_restore_seeds_trip_time);
    RUN_TEST(test_restore_then_update_continues_trip_time);
    RUN_TEST(test_restore_without_trip_time_starts_at_zero);
    RUN_TEST(test_restore_then_update_continues_from_seed);
    RUN_TEST(test_restore_rejects_negative_values);
    RUN_TEST(test_restore_rejects_nan_from_uninitialised_rtc_memory);
    RUN_TEST(test_restore_rejects_infinity);
    RUN_TEST(test_restore_zero_is_a_clean_trip);
}

#include "../../../projects/server/src/session_accumulator.cpp"
