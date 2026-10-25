#include <unity.h>
#include <math.h>
#include <string.h>
#include "../../../projects/main_display/src/log_row.h"

/** A sample with a full fix at 2026-10-24 13:45:12 and the link online. */
static LogSample full_sample() {
    LogSample s = {};
    s.gps.loc_valid  = true;
    s.gps.time_valid = true;
    s.gps.lat   = -23.550520;
    s.gps.lon   = -46.633308;
    s.gps.alt_m = 760.4f;
    s.gps.year = 2026; s.gps.mon = 10; s.gps.day = 24;
    s.gps.hh = 13;     s.gps.mm = 45;  s.gps.ss = 12;
    s.link_valid = true;
    s.speed_kmh  = 87.3f;
    s.rpm        = 2150;
    return s;
}

static void test_full_row() {
    char buf[128];
    const size_t n = log_format_row(full_sample(), buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("2026-10-24 13:45:12,-23.550520,-46.633308,760,87.3,2150\n", buf);
    TEST_ASSERT_EQUAL_UINT32(strlen(buf), n);
}

static void test_no_position_leaves_gps_cells_empty() {
    LogSample s = full_sample();
    s.gps.loc_valid = false;
    char buf[128];
    log_format_row(s, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("2026-10-24 13:45:12,,,,87.3,2150\n", buf);
}

static void test_unknown_altitude_leaves_alt_empty() {
    LogSample s = full_sample();
    s.gps.alt_m = NAN;
    char buf[128];
    log_format_row(s, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("2026-10-24 13:45:12,-23.550520,-46.633308,,87.3,2150\n", buf);
}

static void test_link_down_leaves_speed_rpm_empty() {
    LogSample s = full_sample();
    s.link_valid = false;
    char buf[128];
    log_format_row(s, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("2026-10-24 13:45:12,-23.550520,-46.633308,760,,\n", buf);
}

static void test_row_too_long_for_buffer_returns_zero() {
    char buf[16];
    TEST_ASSERT_EQUAL_UINT32(0, log_format_row(full_sample(), buf, sizeof(buf)));
}

static void test_file_name_is_zero_padded() {
    gps::Fix f = {};
    f.year = 2026; f.mon = 1; f.day = 5; f.hh = 8; f.mm = 4; f.ss = 9;
    char name[32];
    log_file_name(f, name, sizeof(name));
    TEST_ASSERT_EQUAL_STRING("2026-01-05_08-04-09.csv", name);
    TEST_ASSERT_TRUE(log_is_name(name));
}

static void test_is_name_rejects_foreign_files() {
    TEST_ASSERT_FALSE(log_is_name("notes.txt"));
    TEST_ASSERT_FALSE(log_is_name("2026-10-24.csv"));
    TEST_ASSERT_FALSE(log_is_name("2026-10-24 13-45-12.csv"));
    TEST_ASSERT_FALSE(log_is_name("2026-10-24_13-45-12.csv.bak"));
    TEST_ASSERT_FALSE(log_is_name("2026-1a-24_13-45-12.csv"));
}

void run_log_row_tests() {
    RUN_TEST(test_full_row);
    RUN_TEST(test_no_position_leaves_gps_cells_empty);
    RUN_TEST(test_unknown_altitude_leaves_alt_empty);
    RUN_TEST(test_link_down_leaves_speed_rpm_empty);
    RUN_TEST(test_row_too_long_for_buffer_returns_zero);
    RUN_TEST(test_file_name_is_zero_padded);
    RUN_TEST(test_is_name_rejects_foreign_files);
}

#include "../../../projects/main_display/src/log_row.cpp"
