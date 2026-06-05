#include <unity.h>
#include "pid_translator.h"
#include "pid_map.h"
#include <string.h>
#include <math.h>

static const PidDefinition* find_pid(uint16_t pid_code) {
    for (size_t i = 0; i < PID_MAP_SIZE; i++) {
        if (PID_MAP[i].pid == pid_code) return &PID_MAP[i];
    }
    return nullptr;
}

static const Mode22AdvancedPid* find_m22(uint16_t did, uint8_t data_index) {
    for (size_t i = 0; i < MODE22_ADVANCED_PIDS_SIZE; i++) {
        if (MODE22_ADVANCED_PIDS[i].did == did &&
            MODE22_ADVANCED_PIDS[i].data_index == data_index) {
            return &MODE22_ADVANCED_PIDS[i];
        }
    }
    return nullptr;
}

static void test_translate_rpm_two_bytes() {
    CANFrame frame;
    memset(&frame, 0, sizeof(frame));
    // Real TWAI frame: data[0]=PCI, data[1]=0x41 service, data[2]=PID, data[3]=A, data[4]=B
    frame.data[0] = 0x04; frame.data[1] = 0x41; frame.data[2] = 0x0C;
    frame.data[3] = 0x1A; frame.data[4] = 0xF0;
    const PidDefinition* def = find_pid(PID_RPM);
    float result = PIDTranslator::translate(frame, *def);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1724.0f, result);
}

static void test_translate_speed_one_byte() {
    CANFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.data[0] = 0x03; frame.data[1] = 0x41; frame.data[2] = 0x0D; frame.data[3] = 0x64;
    const PidDefinition* def = find_pid(PID_SPEED);
    float result = PIDTranslator::translate(frame, *def);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, result);
}

static void test_translate_maf_two_bytes() {
    CANFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.data[0] = 0x04; frame.data[1] = 0x41; frame.data[2] = 0x10;
    frame.data[3] = 0x01; frame.data[4] = 0x2C;
    const PidDefinition* def = find_pid(PID_MAF);
    float result = PIDTranslator::translate(frame, *def);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.0f, result);
}

static void test_translate_fuel_rate_two_bytes() {
    CANFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.data[0] = 0x04; frame.data[1] = 0x41; frame.data[2] = 0x5E;
    frame.data[3] = 0x00; frame.data[4] = 0x64;
    const PidDefinition* def = find_pid(PID_FUEL_RATE);
    float result = PIDTranslator::translate(frame, *def);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, result);
}

static void test_translate_coolant_temp_with_offset() {
    CANFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.data[0] = 0x03; frame.data[1] = 0x41; frame.data[2] = 0x05; frame.data[3] = 0x5F;
    const PidDefinition* def = find_pid(PID_COOLANT_TEMP);
    float result = PIDTranslator::translate(frame, *def);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 55.0f, result);
}

static void test_translate_egr_error_with_negative_offset() {
    CANFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.data[0] = 0x03; frame.data[1] = 0x41; frame.data[2] = 0x2D; frame.data[3] = 0x80;
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

// ---- Mode 22 advanced-PID translation (Pajero 4M41) ----

// Reassembled UDS for DID 0x20F2: 62 20 F2 D0 D1 D2 D3 D4 ...
// Fuel temp = D4 - 40. D4 = 0x5A (90) -> 50 °C. D4 lives at uds[7].
static void test_m22_fuel_temp_offset() {
    const Mode22AdvancedPid* def = find_m22(0x20F2, 4);
    TEST_ASSERT_NOT_NULL(def);
    uint8_t uds[] = { 0x62, 0x20, 0xF2, 0x00, 0x00, 0x00, 0x00, 0x5A };
    float v = PIDTranslator::translateMode22(uds, sizeof(uds), *def);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, v);
}

// DID 0x2151: fan duty = D1 (no scaling). D1 = 0x32 (50) at uds[4].
static void test_m22_fan_duty_single_byte() {
    const Mode22AdvancedPid* def = find_m22(0x2151, 1);
    TEST_ASSERT_NOT_NULL(def);
    uint8_t uds[] = { 0x62, 0x21, 0x51, 0x00, 0x32 };
    float v = PIDTranslator::translateMode22(uds, sizeof(uds), *def);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, v);
}

// DID 0x20AB: input speed = (D1*256 + D2) * 0.5. D1,D2 at uds[4],uds[5].
// 0x0FA0 = 4000 -> 2000 rpm.
static void test_m22_input_speed_u16() {
    const Mode22AdvancedPid* def = find_m22(0x20AB, 1);
    TEST_ASSERT_NOT_NULL(def);
    uint8_t uds[] = { 0x62, 0x20, 0xAB, 0x00, 0x0F, 0xA0, 0x00, 0x00 };
    float v = PIDTranslator::translateMode22(uds, sizeof(uds), *def);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2000.0f, v);
}

// DID 0x20AB: output speed = (D3*256 + D4) * 0.5. D3,D4 at uds[6],uds[7].
static void test_m22_output_speed_u16() {
    const Mode22AdvancedPid* def = find_m22(0x20AB, 3);
    TEST_ASSERT_NOT_NULL(def);
    uint8_t uds[] = { 0x62, 0x20, 0xAB, 0x00, 0x00, 0x00, 0x07, 0xD0 };  // 0x07D0=2000 ->1000
    float v = PIDTranslator::translateMode22(uds, sizeof(uds), *def);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1000.0f, v);
}

// A response too short to hold the formula's byte(s) must return NaN.
static void test_m22_short_buffer_returns_nan() {
    const Mode22AdvancedPid* def = find_m22(0x20F2, 4);   // needs uds[7]
    TEST_ASSERT_NOT_NULL(def);
    uint8_t uds[] = { 0x62, 0x20, 0xF2, 0x00 };            // only D0 present
    float v = PIDTranslator::translateMode22(uds, sizeof(uds), *def);
    TEST_ASSERT_TRUE(isnan(v));
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
    RUN_TEST(test_m22_fuel_temp_offset);
    RUN_TEST(test_m22_fan_duty_single_byte);
    RUN_TEST(test_m22_input_speed_u16);
    RUN_TEST(test_m22_output_speed_u16);
    RUN_TEST(test_m22_short_buffer_returns_nan);
}

#include "../../../projects/server/src/pid_translator.cpp"
