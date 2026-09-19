#include <unity.h>
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
}

void run_session_accumulator_tests() {
    RUN_TEST(test_initial_distance_is_zero);
    RUN_TEST(test_initial_fuel_is_zero);
    RUN_TEST(test_initial_avg_consumption_is_zero);
    RUN_TEST(test_update_accumulates_distance_correctly);
    RUN_TEST(test_update_accumulates_fuel_correctly);
    RUN_TEST(test_avg_consumption_computed_correctly);
    RUN_TEST(test_update_small_delta_accumulates_correctly);
    RUN_TEST(test_multiple_updates_accumulate_correctly);
    RUN_TEST(test_zero_speed_does_not_accumulate_distance);
    RUN_TEST(test_zero_fuel_does_not_accumulate_fuel);
    RUN_TEST(test_negative_speed_treated_as_zero);
    RUN_TEST(test_reset_clears_all_accumulators);
}

#include "../../../projects/server/src/session_accumulator.cpp"
