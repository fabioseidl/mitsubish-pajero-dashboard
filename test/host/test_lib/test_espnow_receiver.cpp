#include <unity.h>
#include "espnow_receiver.h"
#include "payload.h"
#include "../mocks/mock_espnow.h"
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

// A payload WITHOUT the DATA_VALID flag must still reach the callback: the
// receiver validates transport framing (length + version) only, and rendering is
// the UI's decision.
//
// Dropping it here would break the connection monitor, which is fed from this very
// callback (see client_simple_hud/src/main.cpp). The server clears DATA_VALID
// whenever RPM/SPEED haven't been answered yet — e.g. ignition on, engine not
// running — so a transport-level drop would report a perfectly healthy server as
// OFFLINE. Clients already skip rendering on the flag themselves
// (main_display/src/app_ui.cpp, dashboard_ui.cpp).
static void test_missing_data_valid_flag_still_invokes_callback() {
    reset_counters();
    ESPNowReceiver rcv;
    rcv.setCallback(test_callback);
    Payload p = make_valid_payload();
    p.flags = 0;
    simulateReceive(rcv, (const uint8_t*)&p, sizeof(p));
    TEST_ASSERT_EQUAL_INT(1, callback_count);
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

// --- Sender filtering ------------------------------------------------------
// ESP-NOW cannot encrypt a broadcast, so the Payload travels in the clear and
// unauthenticated: any ESP32 in range can broadcast a well-formed frame and have
// it rendered. Pinning the server's MAC is the mitigation.

static const uint8_t kServerMac[6] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
static const uint8_t kStrangerMac[6] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };

static void test_unfiltered_receiver_accepts_any_sender() {
    reset_counters();
    ESPNowReceiver rcv;
    rcv.clearExpectedSender();
    rcv.setCallback(test_callback);
    Payload p = make_valid_payload();
    simulateReceive(rcv, (const uint8_t*)&p, sizeof(p), kStrangerMac);
    TEST_ASSERT_EQUAL_INT(1, callback_count);
    rcv.clearExpectedSender();
}

static void test_filter_accepts_matching_sender() {
    reset_counters();
    ESPNowReceiver rcv;
    rcv.setExpectedSender(kServerMac);
    rcv.setCallback(test_callback);
    Payload p = make_valid_payload(3100);
    simulateReceive(rcv, (const uint8_t*)&p, sizeof(p), kServerMac);
    TEST_ASSERT_EQUAL_INT(1, callback_count);
    TEST_ASSERT_EQUAL_UINT16(3100, last_rpm);
    rcv.clearExpectedSender();
}

static void test_filter_rejects_other_sender() {
    reset_counters();
    ESPNowReceiver rcv;
    rcv.setExpectedSender(kServerMac);
    rcv.setCallback(test_callback);
    Payload p = make_valid_payload();
    simulateReceive(rcv, (const uint8_t*)&p, sizeof(p), kStrangerMac);
    TEST_ASSERT_EQUAL_INT(0, callback_count);
    rcv.clearExpectedSender();
}

// A spoofed frame that is byte-identical to a real one must still be rejected on
// the strength of its source address alone.
static void test_filter_rejects_spoofed_but_wellformed_payload() {
    reset_counters();
    ESPNowReceiver rcv;
    rcv.setExpectedSender(kServerMac);
    rcv.setCallback(test_callback);
    Payload spoof = make_valid_payload(9999);
    simulateReceive(rcv, (const uint8_t*)&spoof, sizeof(spoof), kStrangerMac);
    TEST_ASSERT_EQUAL_INT(0, callback_count);
    TEST_ASSERT_EQUAL_UINT16(0, last_rpm);
    rcv.clearExpectedSender();
}

static void test_filter_rejects_unknown_sender() {
    reset_counters();
    ESPNowReceiver rcv;
    rcv.setExpectedSender(kServerMac);
    rcv.setCallback(test_callback);
    Payload p = make_valid_payload();
    simulateReceive(rcv, (const uint8_t*)&p, sizeof(p), nullptr);
    TEST_ASSERT_EQUAL_INT(0, callback_count);
    rcv.clearExpectedSender();
}

static void test_rejected_sender_count_increments() {
    reset_counters();
    ESPNowReceiver rcv;
    rcv.setExpectedSender(kServerMac);
    rcv.setCallback(test_callback);
    uint32_t before = ESPNowReceiver::rejected_sender_count_;
    Payload p = make_valid_payload();
    simulateReceive(rcv, (const uint8_t*)&p, sizeof(p), kStrangerMac);
    simulateReceive(rcv, (const uint8_t*)&p, sizeof(p), kStrangerMac);
    TEST_ASSERT_EQUAL_UINT32(before + 2, ESPNowReceiver::rejected_sender_count_);
    rcv.clearExpectedSender();
}

static void test_clearExpectedSender_reopens_to_any_sender() {
    reset_counters();
    ESPNowReceiver rcv;
    rcv.setExpectedSender(kServerMac);
    rcv.setCallback(test_callback);
    Payload p = make_valid_payload();
    simulateReceive(rcv, (const uint8_t*)&p, sizeof(p), kStrangerMac);
    TEST_ASSERT_EQUAL_INT(0, callback_count);
    rcv.clearExpectedSender();
    simulateReceive(rcv, (const uint8_t*)&p, sizeof(p), kStrangerMac);
    TEST_ASSERT_EQUAL_INT(1, callback_count);
}

void run_espnow_receiver_tests() {
    RUN_TEST(test_valid_payload_invokes_callback);
    RUN_TEST(test_callback_receives_correct_rpm);
    RUN_TEST(test_wrong_version_does_not_invoke_callback);
    RUN_TEST(test_wrong_size_does_not_invoke_callback);
    RUN_TEST(test_missing_data_valid_flag_still_invokes_callback);
    RUN_TEST(test_two_valid_payloads_invoke_callback_twice);
    RUN_TEST(test_null_callback_does_not_crash_on_receive);
    RUN_TEST(test_unfiltered_receiver_accepts_any_sender);
    RUN_TEST(test_filter_accepts_matching_sender);
    RUN_TEST(test_filter_rejects_other_sender);
    RUN_TEST(test_filter_rejects_spoofed_but_wellformed_payload);
    RUN_TEST(test_filter_rejects_unknown_sender);
    RUN_TEST(test_rejected_sender_count_increments);
    RUN_TEST(test_clearExpectedSender_reopens_to_any_sender);
}

#include "../../../lib/core/src/espnow_receiver.cpp"
