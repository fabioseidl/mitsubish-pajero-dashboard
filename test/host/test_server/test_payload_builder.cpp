#include <unity.h>
#include "payload_builder.h"
#include "data_aggregator.h"
#include "session_accumulator.h"
#include "pid_map.h"
#include <math.h>

static void test_build_sets_version_correctly() {
    DataAggregator agg;
    SessionAccumulator sess;
    Payload p = PayloadBuilder::build(agg, sess, 0.0f, 0);
    TEST_ASSERT_EQUAL_UINT8(PAYLOAD_VERSION, p.version);
}

static void test_build_copies_rpm_from_aggregator() {
    DataAggregator agg;
    SessionAccumulator sess;
    agg.update(PID_RPM, 3200.0f);
    Payload p = PayloadBuilder::build(agg, sess, 0.0f, 0);
    TEST_ASSERT_EQUAL_UINT16(3200, p.rpm);
}

static void test_build_copies_speed_from_aggregator() {
    DataAggregator agg;
    SessionAccumulator sess;
    agg.update(PID_SPEED, 80.0f);
    Payload p = PayloadBuilder::build(agg, sess, 0.0f, 0);
    TEST_ASSERT_EQUAL_UINT8(80, p.speed_kmh);
}

static void test_build_copies_fuel_rate_from_aggregator() {
    DataAggregator agg;
    SessionAccumulator sess;
    agg.update(PID_FUEL_RATE, 6.5f);
    Payload p = PayloadBuilder::build(agg, sess, 0.0f, 0);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.5f, p.fuel_rate_l_per_h);
}

static void test_build_copies_instantaneous_consumption() {
    DataAggregator agg;
    SessionAccumulator sess;
    Payload p = PayloadBuilder::build(agg, sess, 12.3f, 0);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.3f, p.consumption_km_per_l);
}

static void test_build_copies_avg_consumption_from_session() {
    DataAggregator agg;
    SessionAccumulator sess;
    sess.update(140.0f, 10.0f, 3600000); // 140km / 10L = 14 km/L
    Payload p = PayloadBuilder::build(agg, sess, 0.0f, 0);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 14.0f, p.avg_consumption_km_per_l);
}

static void test_build_copies_distance_from_session() {
    DataAggregator agg;
    SessionAccumulator sess;
    sess.update(42.5f, 1.0f, 3600000); // 42.5 km in 1h at 42.5 km/h
    Payload p = PayloadBuilder::build(agg, sess, 0.0f, 0);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 42.5f, p.distance_km);
}

static void test_build_copies_trip_time_from_session() {
    DataAggregator agg;
    SessionAccumulator sess;
    sess.update(0.0f, 0.0f, 90000);            // 90 s of engine-on time
    Payload p = PayloadBuilder::build(agg, sess, 0.0f, 0);
    TEST_ASSERT_EQUAL_UINT32(90, p.trip_time_s);
}

static void test_build_copies_mil_status_from_aggregator() {
    DataAggregator agg;
    SessionAccumulator sess;
    agg.updateMilStatus(true);
    Payload p = PayloadBuilder::build(agg, sess, 0.0f, 0);
    TEST_ASSERT_TRUE(p.mil_on);
}

static void test_build_copies_dtc_count_from_aggregator() {
    DataAggregator agg;
    SessionAccumulator sess;
    agg.updateDtcCount(2);
    Payload p = PayloadBuilder::build(agg, sess, 0.0f, 0);
    TEST_ASSERT_EQUAL_UINT8(2, p.dtc_count);
}

static void test_build_sets_data_valid_flag_when_all_pids_present() {
    DataAggregator agg;
    SessionAccumulator sess;
    agg.update(PID_RPM, 1000.0f);
    agg.update(PID_SPEED, 50.0f);
    agg.update(PID_FUEL_RATE, 5.0f);
    Payload p = PayloadBuilder::build(agg, sess, 0.0f, 0);
    TEST_ASSERT_TRUE(p.flags & PAYLOAD_FLAG_DATA_VALID);
}

static void test_build_clears_data_valid_flag_when_pids_missing() {
    DataAggregator agg;
    SessionAccumulator sess;
    agg.update(PID_RPM, 1000.0f);
    Payload p = PayloadBuilder::build(agg, sess, 0.0f, 0);
    TEST_ASSERT_FALSE(p.flags & PAYLOAD_FLAG_DATA_VALID);
}

static void test_build_sets_engine_running_flag_when_rpm_above_400() {
    DataAggregator agg;
    SessionAccumulator sess;
    agg.update(PID_RPM, 800.0f);
    Payload p = PayloadBuilder::build(agg, sess, 0.0f, 0);
    TEST_ASSERT_TRUE(p.flags & PAYLOAD_FLAG_ENGINE_RUNNING);
}

static void test_build_clears_engine_running_flag_when_rpm_is_zero() {
    DataAggregator agg;
    SessionAccumulator sess;
    agg.update(PID_RPM, 0.0f);
    Payload p = PayloadBuilder::build(agg, sess, 0.0f, 0);
    TEST_ASSERT_FALSE(p.flags & PAYLOAD_FLAG_ENGINE_RUNNING);
}

static void test_build_copies_onboard_sensors_from_aggregator() {
    DataAggregator agg;
    SessionAccumulator sess;
    agg.update(PID_SENS_ACCEL_X,      1.5f);
    agg.update(PID_SENS_ACCEL_Y,     -0.5f);
    agg.update(PID_SENS_ACCEL_Z,      0.25f);
    agg.update(PID_SENS_GYRO_X,       0.1f);
    agg.update(PID_SENS_GYRO_Y,      -0.2f);
    agg.update(PID_SENS_GYRO_Z,       0.3f);
    agg.update(PID_SENS_ENV_TEMP,    23.5f);
    agg.update(PID_SENS_ENV_HUMIDITY, 61.0f);
    agg.update(PID_SENS_ENV_PRESSURE, 1013.2f);
    Payload p = PayloadBuilder::build(agg, sess, 0.0f, 0);
    TEST_ASSERT_EQUAL_FLOAT(1.5f,    p.imu_accel_x_ms2);
    TEST_ASSERT_EQUAL_FLOAT(-0.5f,   p.imu_accel_y_ms2);
    TEST_ASSERT_EQUAL_FLOAT(0.25f,   p.imu_accel_z_ms2);
    TEST_ASSERT_EQUAL_FLOAT(0.1f,    p.imu_gyro_x_rads);
    TEST_ASSERT_EQUAL_FLOAT(-0.2f,   p.imu_gyro_y_rads);
    TEST_ASSERT_EQUAL_FLOAT(0.3f,    p.imu_gyro_z_rads);
    TEST_ASSERT_EQUAL_FLOAT(23.5f,   p.env_temp_c);
    TEST_ASSERT_EQUAL_FLOAT(61.0f,   p.env_humidity_pct);
    TEST_ASSERT_EQUAL_FLOAT(1013.2f, p.env_pressure_hpa);
}

static void test_build_sends_nan_for_absent_sensors() {
    DataAggregator agg;
    SessionAccumulator sess;
    Payload p = PayloadBuilder::build(agg, sess, 0.0f, 0);
    TEST_ASSERT_TRUE(isnan(p.imu_accel_x_ms2));
    TEST_ASSERT_TRUE(isnan(p.imu_gyro_z_rads));
    TEST_ASSERT_TRUE(isnan(p.env_temp_c));
    TEST_ASSERT_TRUE(isnan(p.env_pressure_hpa));
}

static void test_build_sends_nan_for_stale_sensors() {
    DataAggregator agg;
    SessionAccumulator sess;
    agg.setNow(1000);
    agg.update(PID_SENS_ENV_TEMP, 23.5f);
    agg.setNow(1000 + PayloadBuilder::SENSOR_STALE_MS + 1);
    Payload p = PayloadBuilder::build(agg, sess, 0.0f, 0);
    TEST_ASSERT_TRUE(isnan(p.env_temp_c));
}

static void test_build_copies_timestamp() {
    DataAggregator agg;
    SessionAccumulator sess;
    Payload p = PayloadBuilder::build(agg, sess, 0.0f, 12345);
    TEST_ASSERT_EQUAL_UINT32(12345, p.timestamp_ms);
}

void run_payload_builder_tests() {
    RUN_TEST(test_build_sets_version_correctly);
    RUN_TEST(test_build_copies_rpm_from_aggregator);
    RUN_TEST(test_build_copies_speed_from_aggregator);
    RUN_TEST(test_build_copies_fuel_rate_from_aggregator);
    RUN_TEST(test_build_copies_instantaneous_consumption);
    RUN_TEST(test_build_copies_avg_consumption_from_session);
    RUN_TEST(test_build_copies_distance_from_session);
    RUN_TEST(test_build_copies_trip_time_from_session);
    RUN_TEST(test_build_copies_mil_status_from_aggregator);
    RUN_TEST(test_build_copies_dtc_count_from_aggregator);
    RUN_TEST(test_build_sets_data_valid_flag_when_all_pids_present);
    RUN_TEST(test_build_clears_data_valid_flag_when_pids_missing);
    RUN_TEST(test_build_sets_engine_running_flag_when_rpm_above_400);
    RUN_TEST(test_build_clears_engine_running_flag_when_rpm_is_zero);
    RUN_TEST(test_build_copies_timestamp);
    RUN_TEST(test_build_copies_onboard_sensors_from_aggregator);
    RUN_TEST(test_build_sends_nan_for_absent_sensors);
    RUN_TEST(test_build_sends_nan_for_stale_sensors);
}

#include "../../../projects/server/src/payload_builder.cpp"
