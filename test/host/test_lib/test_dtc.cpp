#include <unity.h>
#include <string.h>
#include "dtc.h"

// ---- dtcToString: the two-bit letter / two-bit digit / 12-bit hex split ----

static void test_code_letters_cover_all_four_banks() {
    char s[6];
    dtcToString(0x01, 0x23, s); TEST_ASSERT_EQUAL_STRING("P0123", s);
    dtcToString(0x41, 0x23, s); TEST_ASSERT_EQUAL_STRING("C0123", s);
    dtcToString(0x81, 0x23, s); TEST_ASSERT_EQUAL_STRING("B0123", s);
    dtcToString(0xC1, 0x23, s); TEST_ASSERT_EQUAL_STRING("U0123", s);
}

static void test_first_digit_uses_bits_13_12() {
    char s[6];
    dtcToString(0x04, 0x01, s); TEST_ASSERT_EQUAL_STRING("P0401", s);
    dtcToString(0x14, 0x01, s); TEST_ASSERT_EQUAL_STRING("P1401", s);
    dtcToString(0x24, 0x01, s); TEST_ASSERT_EQUAL_STRING("P2401", s);
    dtcToString(0x34, 0x01, s); TEST_ASSERT_EQUAL_STRING("P3401", s);
}

static void test_low_twelve_bits_are_hex_nibbles() {
    char s[6];
    dtcToString(0x02, 0xFF, s); TEST_ASSERT_EQUAL_STRING("P02FF", s);
    dtcToString(0x0A, 0xBC, s); TEST_ASSERT_EQUAL_STRING("P0ABC", s);
}

// The four codes the MUT-III disc carries, as a cross-check against real data.
static void test_decodes_the_mut3_gateway_codes() {
    char s[6];
    dtcToString(0xC0, 0x01, s); TEST_ASSERT_EQUAL_STRING("U0001", s);
    dtcToString(0xC0, 0x19, s); TEST_ASSERT_EQUAL_STRING("U0019", s);
    dtcToString(0xD1, 0x20, s); TEST_ASSERT_EQUAL_STRING("U1120", s);
    dtcToString(0xD1, 0x21, s); TEST_ASSERT_EQUAL_STRING("U1121", s);
}

// ---- dtcParseResponse ----

static void test_parses_response_with_leading_count_byte() {
    // 43 02 0401 0301  -> odd payload, so byte 1 is the count
    const uint8_t r[] = { 0x43, 0x02, 0x04, 0x01, 0x03, 0x01 };
    Dtc out[8];
    const uint8_t n = dtcParseResponse(r, (uint8_t)sizeof(r), DTC_STORED, out, 8);
    TEST_ASSERT_EQUAL_UINT8(2, n);
    TEST_ASSERT_EQUAL_STRING("P0401", out[0].code);
    TEST_ASSERT_EQUAL_STRING("P0301", out[1].code);
    TEST_ASSERT_EQUAL_UINT8(DTC_STORED, out[0].kind);
}

static void test_parses_response_without_count_byte() {
    // 43 0401 0301 -> even payload, every byte pair is a code
    const uint8_t r[] = { 0x43, 0x04, 0x01, 0x03, 0x01 };
    Dtc out[8];
    const uint8_t n = dtcParseResponse(r, (uint8_t)sizeof(r), DTC_STORED, out, 8);
    TEST_ASSERT_EQUAL_UINT8(2, n);
    TEST_ASSERT_EQUAL_STRING("P0401", out[0].code);
    TEST_ASSERT_EQUAL_STRING("P0301", out[1].code);
}

static void test_skips_zero_padding() {
    const uint8_t r[] = { 0x43, 0x01, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00 };
    Dtc out[8];
    const uint8_t n = dtcParseResponse(r, (uint8_t)sizeof(r), DTC_STORED, out, 8);
    TEST_ASSERT_EQUAL_UINT8(1, n);
    TEST_ASSERT_EQUAL_STRING("P0401", out[0].code);
}

static void test_no_codes_when_response_is_empty_list() {
    const uint8_t r[] = { 0x43, 0x00 };
    Dtc out[8];
    TEST_ASSERT_EQUAL_UINT8(0, dtcParseResponse(r, (uint8_t)sizeof(r), DTC_STORED, out, 8));
}

static void test_respects_max_out() {
    const uint8_t r[] = { 0x43, 0x03, 0x04, 0x01, 0x03, 0x01, 0x02, 0x99 };
    Dtc out[2];
    TEST_ASSERT_EQUAL_UINT8(2, dtcParseResponse(r, (uint8_t)sizeof(r), DTC_STORED, out, 2));
}

static void test_rejects_null_and_short_input() {
    Dtc out[4];
    const uint8_t r[] = { 0x43 };
    TEST_ASSERT_EQUAL_UINT8(0, dtcParseResponse(nullptr, 4, DTC_STORED, out, 4));
    TEST_ASSERT_EQUAL_UINT8(0, dtcParseResponse(r, 0, DTC_STORED, out, 4));
    TEST_ASSERT_EQUAL_UINT8(0, dtcParseResponse(r, 1, DTC_STORED, out, 4));
}

static void test_kind_is_carried_through() {
    const uint8_t r[] = { 0x4A, 0x04, 0x01 };
    Dtc out[2];
    dtcParseResponse(r, (uint8_t)sizeof(r), DTC_PERMANENT, out, 2);
    TEST_ASSERT_EQUAL_UINT8(DTC_PERMANENT, out[0].kind);
}

// ---- decodeMonitorStatus ----

static void test_mil_off_and_no_codes() {
    const MonitorStatus s = decodeMonitorStatus(0x00, 0x00, 0x00, 0x00);
    TEST_ASSERT_FALSE(s.mil);
    TEST_ASSERT_EQUAL_UINT8(0, s.dtc_count);
}

static void test_mil_bit_is_separate_from_the_count() {
    const MonitorStatus s = decodeMonitorStatus(0x83, 0x00, 0x00, 0x00);
    TEST_ASSERT_TRUE(s.mil);
    TEST_ASSERT_EQUAL_UINT8(3, s.dtc_count);
}

static void test_count_without_mil() {
    const MonitorStatus s = decodeMonitorStatus(0x02, 0x00, 0x00, 0x00);
    TEST_ASSERT_FALSE(s.mil);
    TEST_ASSERT_EQUAL_UINT8(2, s.dtc_count);
}

// Readiness bits are inverted: a set bit means the monitor has NOT completed.
static void test_readiness_bits_are_inverted() {
    const MonitorStatus all_done = decodeMonitorStatus(0x00, 0x07, 0x00, 0x00);
    TEST_ASSERT_TRUE(all_done.misfire_supported);
    TEST_ASSERT_TRUE(all_done.fuel_supported);
    TEST_ASSERT_TRUE(all_done.components_supported);
    TEST_ASSERT_TRUE(all_done.misfire_ready);
    TEST_ASSERT_TRUE(all_done.fuel_ready);
    TEST_ASSERT_TRUE(all_done.components_ready);

    const MonitorStatus none_done = decodeMonitorStatus(0x00, 0x77, 0x00, 0x00);
    TEST_ASSERT_FALSE(none_done.misfire_ready);
    TEST_ASSERT_FALSE(none_done.fuel_ready);
    TEST_ASSERT_FALSE(none_done.components_ready);
}

// ---- dtcDescribe ----

static void test_describes_a_known_generic_code() {
    TEST_ASSERT_NOT_NULL(dtcDescribe("P0401"));
    TEST_ASSERT_NOT_NULL(dtcDescribe("U0001"));
}

static void test_unknown_and_manufacturer_codes_have_no_description() {
    TEST_ASSERT_NULL(dtcDescribe("P1234"));
    TEST_ASSERT_NULL(dtcDescribe("P0000"));
    TEST_ASSERT_NULL(dtcDescribe(nullptr));
}

// A 4-char prefix must not match a 5-char table entry.
static void test_describe_requires_a_full_five_char_match() {
    TEST_ASSERT_NULL(dtcDescribe("P040"));
}

void run_dtc_tests() {
    RUN_TEST(test_code_letters_cover_all_four_banks);
    RUN_TEST(test_first_digit_uses_bits_13_12);
    RUN_TEST(test_low_twelve_bits_are_hex_nibbles);
    RUN_TEST(test_decodes_the_mut3_gateway_codes);
    RUN_TEST(test_parses_response_with_leading_count_byte);
    RUN_TEST(test_parses_response_without_count_byte);
    RUN_TEST(test_skips_zero_padding);
    RUN_TEST(test_no_codes_when_response_is_empty_list);
    RUN_TEST(test_respects_max_out);
    RUN_TEST(test_rejects_null_and_short_input);
    RUN_TEST(test_kind_is_carried_through);
    RUN_TEST(test_mil_off_and_no_codes);
    RUN_TEST(test_mil_bit_is_separate_from_the_count);
    RUN_TEST(test_count_without_mil);
    RUN_TEST(test_readiness_bits_are_inverted);
    RUN_TEST(test_describes_a_known_generic_code);
    RUN_TEST(test_unknown_and_manufacturer_codes_have_no_description);
    RUN_TEST(test_describe_requires_a_full_five_char_match);
}
