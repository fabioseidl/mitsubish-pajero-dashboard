#include <unity.h>
#include "espnow_receiver.h"
#include "payload.h"
#include "./mocks/mock_espnow.h"
#include <string.h>

static int callback_count = 0;
static uint16_t last_rpm  = 0;

static void reset_counters() {
    callback_count = 0;
    last_rpm = 0;
}

static void test_callback(const Payload& p) {
    callback_count++;
    last_rpm = p.rpm;
}

static Payload make_valid_payload(uint16_t rpm = 2500) {
    Payload p;
    memset(&p, 0, sizeof(p));
    p.version = PAYLOAD_VERSION;
    p.rpm     = rpm;
    p.flags   = PAYLOAD_FLAG_DATA_VALID;
    return p;
}

static void test_valid_payload_invokes_callback() {
    reset_counters();
    ESPNowReceiver rcv;
    rcv.setCallback(test_callback);
    Payload p = make_valid_payload();
    simulateReceive(rcv, (const uint8_t*)&p, sizeof(p));
    TEST_ASSERT_EQUAL_INT(1, callback_count);
}

static void test_callback_receives_correct_rpm() {
    reset_counters();
    ESPNowReceiver rcv;
    rcv.setCallback(test_callback);
    Payload p = make_valid_payload(2500);
    simulateReceive(rcv, (const uint8_t*)&p, sizeof(p));
    TEST_ASSERT_EQUAL_UINT16(2500, last_rpm);
}

static void test_wrong_version_does_not_invoke_callback() {
    reset_counters();
    ESPNowReceiver rcv;
    rcv.setCallback(test_callback);
    Payload p = make_valid_payload();
    p.version = PAYLOAD_VERSION + 1;
    simulateReceive(rcv, (const uint8_t*)&p, sizeof(p));
    TEST_ASSERT_EQUAL_INT(0, callback_count);
}

static void test_wrong_size_does_not_invoke_callback() {
    reset_counters();
    ESPNowReceiver rcv;
    rcv.setCallback(test_callback);
    Payload p = make_valid_payload();
    simulateReceive(rcv, (const uint8_t*)&p, (int)sizeof(p) - 1);
    TEST_ASSERT_EQUAL_INT(0, callback_count);
}

static void test_missing_data_valid_flag_does_not_invoke_callback() {
    reset_counters();
    ESPNowReceiver rcv;
    rcv.setCallback(test_callback);
    Payload p = make_valid_payload();
    p.flags = 0;
    simulateReceive(rcv, (const uint8_t*)&p, sizeof(p));
    TEST_ASSERT_EQUAL_INT(0, callback_count);
}

static void test_two_valid_payloads_invoke_callback_twice() {
    reset_counters();
    ESPNowReceiver rcv;
    rcv.setCallback(test_callback);
    Payload p = make_valid_payload();
    simulateReceive(rcv, (const uint8_t*)&p, sizeof(p));
    simulateReceive(rcv, (const uint8_t*)&p, sizeof(p));
    TEST_ASSERT_EQUAL_INT(2, callback_count);
}

static void test_null_callback_does_not_crash_on_receive() {
    ESPNowReceiver rcv;
    rcv.setCallback(nullptr);
    Payload p = make_valid_payload();
    simulateReceive(rcv, (const uint8_t*)&p, sizeof(p));
    TEST_PASS();
}

void run_espnow_receiver_tests() {
    RUN_TEST(test_valid_payload_invokes_callback);
    RUN_TEST(test_callback_receives_correct_rpm);
    RUN_TEST(test_wrong_version_does_not_invoke_callback);
    RUN_TEST(test_wrong_size_does_not_invoke_callback);
    RUN_TEST(test_missing_data_valid_flag_does_not_invoke_callback);
    RUN_TEST(test_two_valid_payloads_invoke_callback_twice);
    RUN_TEST(test_null_callback_does_not_crash_on_receive);
}

#include "../../../lib/core/src/espnow_receiver.cpp"
