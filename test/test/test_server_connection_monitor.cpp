#include <unity.h>
#include "server_connection_monitor.h"

static int  cb_count  = 0;
static bool cb_last   = false;

static void reset_cb() { cb_count = 0; cb_last = false; }
static void status_cb(bool online) { cb_count++; cb_last = online; }

static void test_isOnline_returns_false_before_first_payload() {
    ServerConnectionMonitor mon(2000);
    TEST_ASSERT_FALSE(mon.isOnline());
}

static void test_isOnline_returns_true_after_first_payload() {
    ServerConnectionMonitor mon(2000);
    mon.onPayloadReceived(1000);
    mon.tick(1000);
    TEST_ASSERT_TRUE(mon.isOnline());
}

static void test_remains_online_within_timeout() {
    ServerConnectionMonitor mon(2000);
    mon.onPayloadReceived(0);
    mon.tick(1999);
    TEST_ASSERT_TRUE(mon.isOnline());
}

static void test_goes_offline_when_timeout_elapses() {
    ServerConnectionMonitor mon(2000);
    mon.onPayloadReceived(0);
    mon.tick(0);    // goes online
    mon.tick(2000); // timeout elapsed
    TEST_ASSERT_FALSE(mon.isOnline());
}

static void test_callback_fired_once_on_online_transition() {
    reset_cb();
    ServerConnectionMonitor mon(2000);
    mon.setStatusChangeCallback(status_cb);
    mon.tick(0);                   // still offline
    mon.onPayloadReceived(100);
    mon.tick(100);                 // -> online
    mon.tick(200);                 // still online
    TEST_ASSERT_EQUAL_INT(1, cb_count);
    TEST_ASSERT_TRUE(cb_last);
}

static void test_callback_fired_once_on_offline_transition() {
    reset_cb();
    ServerConnectionMonitor mon(2000);
    mon.setStatusChangeCallback(status_cb);
    mon.onPayloadReceived(0);
    mon.tick(0);     // -> online
    mon.tick(2000);  // -> offline
    mon.tick(2100);  // still offline
    // first callback: online, second: offline
    TEST_ASSERT_EQUAL_INT(2, cb_count);
    TEST_ASSERT_FALSE(cb_last);
}

static void test_callback_fired_on_recovery_after_offline() {
    reset_cb();
    ServerConnectionMonitor mon(2000);
    mon.setStatusChangeCallback(status_cb);
    mon.onPayloadReceived(0);
    mon.tick(0);     // -> online (cb: true)
    mon.tick(2000);  // -> offline (cb: false)
    mon.onPayloadReceived(2500);
    mon.tick(2500);  // -> online again (cb: true)
    TEST_ASSERT_EQUAL_INT(3, cb_count);
    TEST_ASSERT_TRUE(cb_last);
}

static void test_no_callback_when_not_registered() {
    ServerConnectionMonitor mon(2000);
    mon.setStatusChangeCallback(nullptr);
    mon.onPayloadReceived(0);
    mon.tick(0);
    mon.tick(2000);
    TEST_PASS();
}

static void test_custom_timeout_respected() {
    ServerConnectionMonitor mon(500);
    mon.onPayloadReceived(0);
    mon.tick(499);
    TEST_ASSERT_TRUE(mon.isOnline());
    mon.tick(500);
    TEST_ASSERT_FALSE(mon.isOnline());
}

static void test_repeated_payloads_keep_server_online() {
    ServerConnectionMonitor mon(2000);
    mon.onPayloadReceived(0);
    mon.tick(100);
    mon.onPayloadReceived(100);
    mon.tick(1900);
    mon.onPayloadReceived(1900);
    mon.tick(3800); // last payload at 1900, timeout at 3900 -> still online
    TEST_ASSERT_TRUE(mon.isOnline());
}

void run_server_connection_monitor_tests() {
    RUN_TEST(test_isOnline_returns_false_before_first_payload);
    RUN_TEST(test_isOnline_returns_true_after_first_payload);
    RUN_TEST(test_remains_online_within_timeout);
    RUN_TEST(test_goes_offline_when_timeout_elapses);
    RUN_TEST(test_callback_fired_once_on_online_transition);
    RUN_TEST(test_callback_fired_once_on_offline_transition);
    RUN_TEST(test_callback_fired_on_recovery_after_offline);
    RUN_TEST(test_no_callback_when_not_registered);
    RUN_TEST(test_custom_timeout_respected);
    RUN_TEST(test_repeated_payloads_keep_server_online);
}

#include "../../../lib/core/src/server_connection_monitor.cpp"
