#include <unity.h>
#include "data_aggregator.h"
#include "pid_map.h"

static void test_get_before_update_returns_zero() {
    DataAggregator agg;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, agg.get(PID_RPM));
}

static void test_isValid_before_update_returns_false() {
    DataAggregator agg;
    TEST_ASSERT_FALSE(agg.isValid(PID_RPM));
}

static void test_update_then_get_returns_value() {
    DataAggregator agg;
    agg.update(PID_RPM, 3000.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3000.0f, agg.get(PID_RPM));
}

static void test_isValid_after_update_returns_true() {
    DataAggregator agg;
    agg.update(PID_RPM, 3000.0f);
    TEST_ASSERT_TRUE(agg.isValid(PID_RPM));
}

static void test_update_overwrites_previous_value() {
    DataAggregator agg;
    agg.update(PID_RPM, 1000.0f);
    agg.update(PID_RPM, 2000.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2000.0f, agg.get(PID_RPM));
}

static void test_allRequiredPidsReceived_false_when_partial() {
    DataAggregator agg;
    agg.update(PID_RPM, 1000.0f);
    agg.update(PID_SPEED, 50.0f);
    TEST_ASSERT_FALSE(agg.allRequiredPidsReceived());
}

static void test_allRequiredPidsReceived_true_when_all_present() {
    DataAggregator agg;
    agg.update(PID_RPM, 1000.0f);
    agg.update(PID_SPEED, 50.0f);
    agg.update(PID_FUEL_RATE, 5.0f);
    TEST_ASSERT_TRUE(agg.allRequiredPidsReceived());
}

static void test_update_mil_status_stored_and_retrieved() {
    DataAggregator agg;
    agg.updateMilStatus(true);
    TEST_ASSERT_TRUE(agg.getMilStatus());
}

static void test_update_dtc_count_stored_and_retrieved() {
    DataAggregator agg;
    agg.updateDtcCount(3);
    TEST_ASSERT_EQUAL_UINT8(3, agg.getDtcCount());
}

static void test_reset_invalidates_all_values() {
    DataAggregator agg;
    agg.update(PID_RPM, 1000.0f);
    agg.update(PID_SPEED, 50.0f);
    agg.updateMilStatus(true);
    agg.updateDtcCount(5);
    agg.reset();
    TEST_ASSERT_FALSE(agg.isValid(PID_RPM));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, agg.get(PID_RPM));
    TEST_ASSERT_FALSE(agg.getMilStatus());
    TEST_ASSERT_EQUAL_UINT8(0, agg.getDtcCount());
}

void run_data_aggregator_tests() {
    RUN_TEST(test_get_before_update_returns_zero);
    RUN_TEST(test_isValid_before_update_returns_false);
    RUN_TEST(test_update_then_get_returns_value);
    RUN_TEST(test_isValid_after_update_returns_true);
    RUN_TEST(test_update_overwrites_previous_value);
    RUN_TEST(test_allRequiredPidsReceived_false_when_partial);
    RUN_TEST(test_allRequiredPidsReceived_true_when_all_present);
    RUN_TEST(test_update_mil_status_stored_and_retrieved);
    RUN_TEST(test_update_dtc_count_stored_and_retrieved);
    RUN_TEST(test_reset_invalidates_all_values);
}

#include "../../../projects/server/src/data_aggregator.cpp"
