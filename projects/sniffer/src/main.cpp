#include <Arduino.h>
#include "driver/twai.h"

static constexpr gpio_num_t PIN_CAN_TX = GPIO_NUM_5;
static constexpr gpio_num_t PIN_CAN_RX = GPIO_NUM_4;
static const uint32_t USB_BAUD = 115200;

static uint64_t unix_base_us = 0;   // Unix time in microseconds at sync point
static uint64_t micros_at_sync = 0; // micros() value when sync was received

static bool canBegin() {
    twai_general_config_t g_cfg = TWAI_GENERAL_CONFIG_DEFAULT(PIN_CAN_TX, PIN_CAN_RX, TWAI_MODE_LISTEN_ONLY);
    twai_timing_config_t t_cfg = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_cfg = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_cfg, &t_cfg, &f_cfg) != ESP_OK) return false;
    if (twai_start() != ESP_OK) {
        twai_driver_uninstall();
        return false;
    }
    return true;
}

static void handleSerial() {
    if (!Serial.available()) return;

    String line = Serial.readStringUntil('\n');
    line.trim();

    // Expects format: T1716394391
    if (line.length() < 2 || line[0] != 'T') return;

    uint64_t unix_sec = strtoull(line.c_str() + 1, nullptr, 10);
    unix_base_us = unix_sec * 1000000ULL;
    micros_at_sync = micros();
}

static uint64_t currentTimestampUs() {
    if (unix_base_us == 0) return micros(); // fallback: time since boot
    return unix_base_us + (micros() - micros_at_sync);
}

void setup() {
    Serial.begin(USB_BAUD);
    delay(400);
    canBegin();
}

void loop() {
    handleSerial();

    twai_message_t rx = {};
    if (twai_receive(&rx, pdMS_TO_TICKS(2)) != ESP_OK) return;

    uint64_t ts = currentTimestampUs();
    uint32_t ts_sec  = ts / 1000000ULL;
    uint32_t ts_usec = ts % 1000000ULL;

    Serial.printf("(%lu.%06lu) can0 %X#", ts_sec, ts_usec, rx.identifier);

    for (uint8_t i = 0; i < rx.data_length_code; i++) {
        Serial.printf("%02X", rx.data[i]);
    }

    Serial.println();
}