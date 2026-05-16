#ifndef UNIT_TEST
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "can_driver.h"
#include "pid_dictionary.h"
#include "pid_translator.h"
#include "data_aggregator.h"
#include "derived_calculator.h"
#include "session_accumulator.h"
#include "payload_builder.h"
#include "espnow_broadcaster.h"
#include "pid_map.h"
#include "security_config.h"

static DataAggregator aggregator;

static void can_rx_task(void* /*param*/) {
    CANDriver     driver;
    PIDDictionary dictionary;
    bool          car_can_connected = false;

    Serial.println("Initializing TWAI...");
    if (!driver.begin()) {
        Serial.println("ERROR: Failed to initialize TWAI");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    Serial.println("TWAI initialized successfully");

    while (true) {
        if (driver.isFrameAvailable()) {
            CANFrame frame;
            if (driver.readFrame(frame)) {
                if (!car_can_connected) {
                    car_can_connected = true;
                    Serial.println("Connected to vehicle CAN bus");
                }
                Serial.printf("CAN message received: ID=0x%03X EXT=%d DLC=%d DATA=", frame.id, frame.is_extended ? 1 : 0, frame.dlc);
                for (uint8_t i = 0; i < frame.dlc; ++i) {
                    Serial.printf(" %02X", frame.data[i]);
                }
                Serial.println();

                if (frame.data[2] == PID_MONITOR_STATUS) {
                    aggregator.updateMilStatus(PIDTranslator::extractMilStatus(frame));
                    aggregator.updateDtcCount(PIDTranslator::extractDtcCount(frame));
                } else {
                    const PidDefinition* def = dictionary.lookup(frame.id, frame.data[2]);
                    if (def != nullptr) {
                        float value = PIDTranslator::translate(frame, *def);
                        aggregator.update(def->pid, value);
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void broadcast_task(void* /*param*/) {
    SessionAccumulator session;
    ESPNowBroadcaster  broadcaster;
    uint32_t           last_tick_ms = 0;

    bool begin_ok = broadcaster.begin(PMK_KEY);
    Serial.printf("ESP-NOW begin=%d add_peer_err=%d send_err_init=%d\n",
                  begin_ok, (int)broadcaster.lastAddPeerErr(), (int)broadcaster.lastSendErr());

    while (true) {
        uint32_t now_ms   = (uint32_t)(esp_timer_get_time() / 1000);
        uint32_t delta_ms = now_ms - last_tick_ms;
        last_tick_ms      = now_ms;

        float speed       = aggregator.get(PID_SPEED);
        float fuel_rate   = aggregator.get(PID_FUEL_RATE);
        float consumption = DerivedCalculator::computeConsumption(aggregator);

        session.update(speed, fuel_rate, delta_ms);

        Payload payload = PayloadBuilder::build(aggregator, session, consumption, now_ms);
        broadcaster.send(payload);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    Serial.println("Server starting...");

    xTaskCreatePinnedToCore(can_rx_task,    "can_rx",    4096, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(broadcast_task, "broadcast", 4096, nullptr, 3, nullptr, 0);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
#endif
