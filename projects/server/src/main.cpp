#ifndef UNIT_TEST
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <WiFi.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_event.h>

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
#include <esp_log.h>

static const char* TAG = "server";

static DataAggregator aggregator;

static const uint32_t OBD_REQUEST_ID = 0x7DF;
static const uint32_t OBD_POLL_INTERVAL_MS = 50;

static CANFrame makeOBDRequest(uint8_t pid) {
    CANFrame f = {};
    f.id          = OBD_REQUEST_ID;
    f.dlc         = 8;
    f.is_extended = false;
    f.data[0]     = 0x02;
    f.data[1]     = 0x01;
    f.data[2]     = pid;
    return f;
}

static void can_rx_task(void* /*param*/) {
    CANDriver     driver;
    PIDDictionary dictionary;
    bool          car_can_connected = false;

    static const uint8_t POLL_PIDS[] = {
        PID_MONITOR_STATUS,
        PID_RPM,
        PID_SPEED,
        PID_FUEL_RATE,
        PID_COOLANT_TEMP,
        PID_ENGINE_LOAD,
        PID_THROTTLE,
    };
    static const size_t POLL_COUNT = sizeof(POLL_PIDS) / sizeof(POLL_PIDS[0]);
    size_t   poll_idx     = 0;
    uint32_t last_poll_ms = 0;

    Serial.println("Initializing TWAI...");
    if (!driver.begin()) {
        Serial.println("ERROR: Failed to initialize TWAI");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    Serial.println("TWAI initialized successfully");

    while (true) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

        if (now_ms - last_poll_ms >= OBD_POLL_INTERVAL_MS) {
            driver.sendFrame(makeOBDRequest(POLL_PIDS[poll_idx]));
            poll_idx   = (poll_idx + 1) % POLL_COUNT;
            last_poll_ms = now_ms;
        }

        if (driver.isFrameAvailable()) {
            CANFrame frame;
            if (driver.readFrame(frame)) {
                if (!car_can_connected) {
                    car_can_connected = true;
                    Serial.println("Connected to vehicle CAN bus");
                }

                if (frame.id != 0x7E8) {
                    vTaskDelay(pdMS_TO_TICKS(1));
                    continue;
                }

                Serial.printf("CAN ID=0x%03X PID=0x%02X DLC=%d\n", frame.id, frame.data[2], frame.dlc);

                if (frame.data[2] == PID_MONITOR_STATUS) {
                    Serial.println("  -> Updating MIL/DTC status");
                    aggregator.updateMilStatus(PIDTranslator::extractMilStatus(frame));
                    aggregator.updateDtcCount(PIDTranslator::extractDtcCount(frame));
                } else {
                    const PidDefinition* def = dictionary.lookup(frame.id, frame.data[2]);
                    if (def != nullptr) {
                        float value = PIDTranslator::translate(frame, *def);
                        Serial.printf("  -> Found PID 0x%02X, value=%.2f\n", frame.data[2], value);
                        aggregator.update(def->pid, value);
                    } else {
                        Serial.printf("  -> PID 0x%02X not found in dictionary\n", frame.data[2]);
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void broadcast_task(void* /*param*/) {
    Serial.println("broadcast_task started");
    SessionAccumulator session;
    ESPNowBroadcaster  broadcaster;
    uint32_t           last_tick_ms = 0;
    uint32_t           send_count = 0;
    uint32_t           fail_count = 0;

    Serial.println("About to call broadcaster.begin()");
    bool begin_ok = broadcaster.begin(PMK_KEY);
    Serial.printf("broadcaster.begin() returned: %d, add_peer_err=%d, send_err=%d\n",
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
        bool sent = broadcaster.send(payload);
        sent ? ++send_count : ++fail_count;

        if (fail_count % 50 == 1) {
            Serial.printf("send_ok=%d sent=%lu failed=%lu\n",
                         sent, (unsigned long)send_count, (unsigned long)fail_count);
        }
        Serial.printf("timestamp=%lu ms, rpm=%u, speed=%u km/h, sent=%d\n",
                     (unsigned long)payload.timestamp_ms, (unsigned)payload.rpm, (unsigned)payload.speed_kmh, sent);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void setup() {
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    Serial.begin(115200);
    while (!Serial) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    Serial.println("Server starting...");

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    xTaskCreatePinnedToCore(can_rx_task,    "can_rx",    4096, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(broadcast_task, "broadcast", 4096, nullptr, 3, nullptr, 0);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
#endif
