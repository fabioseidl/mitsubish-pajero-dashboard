#include <unity.h>
#include "pid_translator.h"
#include "pid_map.h"
#include <string.h>

static const PidDefinition* find_pid(uint16_t pid_code) {
    for (size_t i = 0; i < PID_MAP_SIZE; i++) {
        if (PID_MAP[i].pid == pid_code) return &PID_MAP[i];
    }
    return nullptr;
}

static void test_translate_rpm_two_bytes() {
    CANFrame frame;
    memset(&frame, 0, sizeof(frame));
    // byte_A at data[2], byte_B at data[3]
    frame.data[0] = 0x41; frame.data[1] = 0x0C;
    frame.data[2] = 0x1A; frame.data[3] = 0xF0;
    const PidDefinition* def = find_pid(PID_RPM);
    float result = PIDTranslator::translate(frame, *def);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1724.0f, result);
}

static void test_translate_speed_one_byte() {
    CANFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.data[0] = 0x41; frame.data[1] = 0x0D; frame.data[2] = 0x64;
    const PidDefinition* def = find_pid(PID_SPEED);
    float result = PIDTranslator::translate(frame, *def);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, result);
}

static void test_translate_maf_two_bytes() {
    CANFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.data[0] = 0x41; frame.data[1] = 0x10;
    frame.data[2] = 0x01; frame.data[3] = 0x2C;
    const PidDefinition* def = find_pid(PID_MAF);
    float result = PIDTranslator::translate(frame, *def);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.0f, result);
}

static void test_translate_fuel_rate_two_bytes() {
    CANFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.data[0] = 0x41; frame.data[1] = 0x5E;
    frame.data[2] = 0x00; frame.data[3] = 0x64;
    const PidDefinition* def = find_pid(PID_FUEL_RATE);
    float result = PIDTranslator::translate(frame, *def);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, result);
}

static void test_translate_coolant_temp_with_offset() {
    CANFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.data[0] = 0x41; frame.data[1] = 0x05; frame.data[2] = 0x5F;
    const PidDefinition* def = find_pid(PID_COOLANT_TEMP);
    float result = PIDTranslator::translate(frame, *def);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 55.0f, result);
}

static void test_translate_egr_error_with_negative_offset() {
    CANFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.data[0] = 0x41; frame.data[1] = 0x2D; frame.data[2] = 0x80;
    const PidDefinition* def = find_pid(PID_EGR_ERROR);
    float result = PIDTranslator::translate(frame, *def);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, result);
}

static void test_translate_all_zero_bytes_returns_offset() {
    CANFrame frame;
    memset(&frame, 0, sizeof(frame));
    for (size_t i = 0; i < PID_MAP_SIZE; i++) {
        if (PID_MAP[i].formula_type != FORMULA_LINEAR) continue;
        float result = PIDTranslator::translate(frame, PID_MAP[i]);
        TEST_ASSERT_FLOAT_WITHIN(0.001f, PID_MAP[i].offset, result);
    }
}

static void test_extract_mil_status_bit7_set() {
    CANFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.data[3] = 0x80;
    TEST_ASSERT_TRUE(PIDTranslator::extractMilStatus(frame));
}

static void test_extract_mil_status_bit7_clear() {
    CANFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.data[3] = 0x05;
    TEST_ASSERT_FALSE(PIDTranslator::extractMilStatus(frame));
}

static void test_extract_dtc_count_bits_0_to_6() {
    CANFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.data[3] = 0x85;
    TEST_ASSERT_EQUAL_UINT8(5, PIDTranslator::extractDtcCount(frame));
}

static void test_extract_dtc_count_zero() {
    CANFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.data[3] = 0x00;
    TEST_ASSERT_EQUAL_UINT8(0, PIDTranslator::extractDtcCount(frame));
}

void run_pid_translator_tests() {
    RUN_TEST(test_translate_rpm_two_bytes);
    RUN_TEST(test_translate_speed_one_byte);
    RUN_TEST(test_translate_maf_two_bytes);
    RUN_TEST(test_translate_fuel_rate_two_bytes);
    RUN_TEST(test_translate_coolant_temp_with_offset);
    RUN_TEST(test_translate_egr_error_with_negative_offset);
    RUN_TEST(test_translate_all_zero_bytes_returns_offset);
    RUN_TEST(test_extract_mil_status_bit7_set);
    RUN_TEST(test_extract_mil_status_bit7_clear);
    RUN_TEST(test_extract_dtc_count_bits_0_to_6);
    RUN_TEST(test_extract_dtc_count_zero);
}

#include "../../../projects/server/src/pid_translator.cpp"
