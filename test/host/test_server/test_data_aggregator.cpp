#include <unity.h>
#include <stdint.h>
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

// "Required" is RPM + SPEED only — deliberately NOT fuel rate.
//
// This gates PAYLOAD_FLAG_DATA_VALID, which clients use to decide whether to
// render anything at all. This vehicle's ECU never answers PID 0x5E (fuel rate);
// it is polled, but valid_[PID_FUEL_RATE] stays false forever, which is exactly
// why DerivedCalculator computes the rate from CAN 0x608 / MAF instead. Adding
// 0x5E to the required set would therefore clear DATA_VALID permanently and blank
// every client. The original spec listed it before that was discovered.
static void test_allRequiredPidsReceived_false_when_partial() {
    DataAggregator agg;
    agg.update(PID_RPM, 1000.0f);
    TEST_ASSERT_FALSE(agg.allRequiredPidsReceived());
}

static void test_allRequiredPidsReceived_true_when_all_present() {
    DataAggregator agg;
    agg.update(PID_RPM, 1000.0f);
    agg.update(PID_SPEED, 50.0f);
    TEST_ASSERT_TRUE(agg.allRequiredPidsReceived());
}

// Regression guard: an unanswered fuel-rate PID must never hold DATA_VALID down.
static void test_allRequiredPidsReceived_true_without_fuel_rate() {
    DataAggregator agg;
    agg.update(PID_RPM, 1000.0f);
    agg.update(PID_SPEED, 50.0f);
    TEST_ASSERT_FALSE(agg.isValid(PID_FUEL_RATE));
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

// --- Staleness -------------------------------------------------------------
// isValid() latches true forever, which cannot distinguish a live reading from
// one the ECU stopped answering. isFresh() is what a decision should consult.

static void test_isFresh_false_before_any_update() {
    DataAggregator agg;
    TEST_ASSERT_FALSE(agg.isFresh(PID_RPM, 1000));
}

static void test_isFresh_true_immediately_after_update() {
    DataAggregator agg;
    agg.setNow(5000);
    agg.update(PID_RPM, 2000.0f);
    TEST_ASSERT_TRUE(agg.isFresh(PID_RPM, 1000));
}

static void test_isFresh_true_within_window() {
    DataAggregator agg;
    agg.setNow(5000);
    agg.update(PID_RPM, 2000.0f);
    agg.setNow(5900);
    TEST_ASSERT_TRUE(agg.isFresh(PID_RPM, 1000));
}

static void test_isFresh_false_once_window_elapsed() {
    DataAggregator agg;
    agg.setNow(5000);
    agg.update(PID_RPM, 2000.0f);
    agg.setNow(6001);
    TEST_ASSERT_FALSE(agg.isFresh(PID_RPM, 1000));
}

static void test_isValid_stays_true_after_value_goes_stale() {
    DataAggregator agg;
    agg.setNow(5000);
    agg.update(PID_RPM, 2000.0f);
    agg.setNow(500000);
    TEST_ASSERT_TRUE(agg.isValid(PID_RPM));    // ever-received: still true
    TEST_ASSERT_FALSE(agg.isFresh(PID_RPM, 1000));
}

static void test_update_refreshes_age() {
    DataAggregator agg;
    agg.setNow(1000);
    agg.update(PID_RPM, 2000.0f);
    agg.setNow(9000);
    TEST_ASSERT_FALSE(agg.isFresh(PID_RPM, 1000));
    agg.update(PID_RPM, 2100.0f);              // ECU answered again
    TEST_ASSERT_TRUE(agg.isFresh(PID_RPM, 1000));
}

static void test_ageMs_is_max_for_never_received() {
    DataAggregator agg;
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, agg.ageMs(PID_RPM));
}

static void test_ageMs_reports_elapsed_time() {
    DataAggregator agg;
    agg.setNow(1000);
    agg.update(PID_SPEED, 50.0f);
    agg.setNow(3500);
    TEST_ASSERT_EQUAL_UINT32(2500, agg.ageMs(PID_SPEED));
}

// The server runs for weeks; esp_timer millis wrap every ~49 days. Unsigned
// subtraction must keep reporting a small age across that boundary rather than
// declaring every value stale for the next 49 days.
static void test_ageMs_survives_millis_wraparound() {
    DataAggregator agg;
    agg.setNow(0xFFFFFF00u);
    agg.update(PID_SPEED, 50.0f);
    agg.setNow(0x00000064u);                   // wrapped; 356 ms later
    TEST_ASSERT_EQUAL_UINT32(356, agg.ageMs(PID_SPEED));
    TEST_ASSERT_TRUE(agg.isFresh(PID_SPEED, 1000));
}

static void test_reset_clears_freshness() {
    DataAggregator agg;
    agg.setNow(1000);
    agg.update(PID_RPM, 2000.0f);
    agg.reset();
    TEST_ASSERT_FALSE(agg.isFresh(PID_RPM, 1000));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, agg.ageMs(PID_RPM));
}

void run_data_aggregator_tests() {
    RUN_TEST(test_get_before_update_returns_zero);
    RUN_TEST(test_isValid_before_update_returns_false);
    RUN_TEST(test_update_then_get_returns_value);
    RUN_TEST(test_isValid_after_update_returns_true);
    RUN_TEST(test_update_overwrites_previous_value);
    RUN_TEST(test_allRequiredPidsReceived_false_when_partial);
    RUN_TEST(test_allRequiredPidsReceived_true_when_all_present);
    RUN_TEST(test_allRequiredPidsReceived_true_without_fuel_rate);
    RUN_TEST(test_update_mil_status_stored_and_retrieved);
    RUN_TEST(test_update_dtc_count_stored_and_retrieved);
    RUN_TEST(test_reset_invalidates_all_values);
    RUN_TEST(test_isFresh_false_before_any_update);
    RUN_TEST(test_isFresh_true_immediately_after_update);
    RUN_TEST(test_isFresh_true_within_window);
    RUN_TEST(test_isFresh_false_once_window_elapsed);
    RUN_TEST(test_isValid_stays_true_after_value_goes_stale);
    RUN_TEST(test_update_refreshes_age);
    RUN_TEST(test_ageMs_is_max_for_never_received);
    RUN_TEST(test_ageMs_reports_elapsed_time);
    RUN_TEST(test_ageMs_survives_millis_wraparound);
    RUN_TEST(test_reset_clears_freshness);
}

#include "../../../projects/server/src/data_aggregator.cpp"
