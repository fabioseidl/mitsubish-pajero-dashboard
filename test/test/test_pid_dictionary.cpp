#include <unity.h>
#include "pid_dictionary.h"
#include "pid_map.h"

static void test_lookup_known_pid_returns_definition() {
    PIDDictionary dict;
    const PidDefinition* def = dict.lookup(0x7E8, PID_RPM);
    TEST_ASSERT_NOT_NULL(def);
    TEST_ASSERT_EQUAL_UINT16(PID_RPM, def->pid);
}

static void test_lookup_unknown_pid_returns_null() {
    PIDDictionary dict;
    const PidDefinition* def = dict.lookup(0x7E8, 0xFF);
    TEST_ASSERT_NULL(def);
}

static void test_lookup_unknown_can_id_returns_null() {
    PIDDictionary dict;
    const PidDefinition* def = dict.lookup(0x123, PID_RPM);
    TEST_ASSERT_NULL(def);
}

static void test_lookup_all_confirmed_pids_succeed() {
    PIDDictionary dict;
    for (size_t i = 0; i < PID_MAP_SIZE; i++) {
        if (!PID_MAP[i].verified) continue;
        const PidDefinition* def = dict.lookup(0x7E8, (uint8_t)PID_MAP[i].pid);
        TEST_ASSERT_NOT_NULL(def);
    }
}

static void test_is_known_id_true_for_obd_response_address() {
    PIDDictionary dict;
    TEST_ASSERT_TRUE(dict.isKnownId(0x7E8));
}

static void test_is_known_id_false_for_unknown_address() {
    PIDDictionary dict;
    TEST_ASSERT_FALSE(dict.isKnownId(0x001));
}

static void test_size_returns_pid_map_size() {
    PIDDictionary dict;
    TEST_ASSERT_EQUAL(PID_MAP_SIZE, dict.size());
}

void run_pid_dictionary_tests() {
    RUN_TEST(test_lookup_known_pid_returns_definition);
    RUN_TEST(test_lookup_unknown_pid_returns_null);
    RUN_TEST(test_lookup_unknown_can_id_returns_null);
    RUN_TEST(test_lookup_all_confirmed_pids_succeed);
    RUN_TEST(test_is_known_id_true_for_obd_response_address);
    RUN_TEST(test_is_known_id_false_for_unknown_address);
    RUN_TEST(test_size_returns_pid_map_size);
}

#include "../../../projects/server/src/pid_dictionary.cpp"
