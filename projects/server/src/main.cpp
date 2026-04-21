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
#include "pin_config.h"
#include "security_config.h"

static DataAggregator aggregator;

static void can_rx_task(void* /*param*/) {
    CANDriver     driver(PIN_CAN_CS, PIN_CAN_INT);
    PIDDictionary dictionary;

    driver.begin();

    while (true) {
        if (driver.isFrameAvailable()) {
            CANFrame frame;
            if (driver.readFrame(frame)) {
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

    broadcaster.begin(PMK_KEY);

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

extern "C" void app_main() {
    xTaskCreatePinnedToCore(can_rx_task,    "can_rx",    4096, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(broadcast_task, "broadcast", 4096, nullptr, 3, nullptr, 0);
}
#endif
