#include <unity.h>

void setUp()    {}
void tearDown() {}

void run_brightness_controller_tests();
void run_espnow_receiver_tests();
void run_server_connection_monitor_tests();

int main() {
    UNITY_BEGIN();
    run_brightness_controller_tests();
    run_espnow_receiver_tests();
    run_server_connection_monitor_tests();
    return UNITY_END();
}
