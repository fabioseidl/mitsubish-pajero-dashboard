#include <unity.h>

void setUp()    {}
void tearDown() {}

void run_pid_translator_tests();
void run_pid_dictionary_tests();
void run_data_aggregator_tests();
void run_derived_calculator_tests();
void run_session_accumulator_tests();
void run_payload_builder_tests();

int main() {
    UNITY_BEGIN();
    run_pid_translator_tests();
    run_pid_dictionary_tests();
    run_data_aggregator_tests();
    run_derived_calculator_tests();
    run_session_accumulator_tests();
    run_payload_builder_tests();
    return UNITY_END();
}
