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
#include "pin_config.h"
#include "security_config.h"
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_wifi.h>

static const char* TAG = "server";

static DataAggregator aggregator;

static const uint32_t OBD_REQUEST_ID = 0x7DF;
static const uint32_t OBD_POLL_INTERVAL_MS = 50;

// --- Energy saving ----------------------------------------------------------
// The board is wired to the always-on OBD-II power, so it must sleep when the
// car is off or it would slowly drain the battery. "Car off" is detected as the
// CAN bus going silent: while the ignition is on the bus carries traffic; when
// it is off the ECUs (and the bus) go quiet. After this much silence we deep
// sleep until the MCP2515 INT pin signals new RX traffic, or a fallback timer.
static const uint32_t CAR_OFF_TIMEOUT_MS    = 5000;
static const uint32_t DEEP_SLEEP_FALLBACK_S = 30;

// Set true once CAN activity is observed. Gates the WiFi/ESP-NOW bring-up so
// that timer-triggered probe wake-ups while the car is off stay cheap (CAN
// only, radio off) instead of burning ~150 mA powering the radio for nothing.
static volatile bool g_car_on = false;

// Build a Mode 01 (OBD-II) request for a single-byte PID.
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

// Build a Mode 22 (UDS ReadDataByIdentifier) request for a 16-bit DID.
static CANFrame makeMode22Request(uint16_t did) {
    CANFrame f = {};
    f.id          = OBD_REQUEST_ID;
    f.dlc         = 8;
    f.is_extended = false;
    f.data[0]     = 0x03;
    f.data[1]     = 0x22;
    f.data[2]     = (did >> 8) & 0xFF;
    f.data[3]     = did & 0xFF;
    return f;
}

// Look up a Mode 22 real PID and return its DataAggregator slot ID.
// Returns 0xFF when not found.
static uint8_t mode22SlotId(uint16_t real_pid) {
    for (size_t i = 0; i < MODE22_PID_MAP_SIZE; ++i) {
        if (MODE22_PID_MAP[i].real_pid == real_pid) {
            return MODE22_PID_MAP[i].slot_id;
        }
    }
    return 0xFF;
}

// Enter deep sleep to save the car battery while the engine is off. Drains the
// CAN controller so its INT line is idle, then arms two wake sources: the
// MCP2515 INT pin going LOW (new RX traffic = ignition back on) and a fallback
// timer. esp_deep_sleep_start() never returns — the chip reboots on wake and
// re-runs setup(), which re-probes the bus and sleeps again if still silent.
static void enterDeepSleep(CANDriver& driver) {
    g_car_on = false;
    Serial.println("[power] CAN bus silent — car OFF, entering deep sleep");
    Serial.flush();

    driver.prepareForSleep();
    esp_wifi_stop();   // ensure the radio is down before we power off

    // GPIO8 (MCP2515 INT) is RTC-capable on the ESP32-S3; INT is active-low and
    // push-pull, so ANY_LOW fires only when a frame is actually received.
    esp_sleep_enable_ext1_wakeup(1ULL << PIN_MCP2515_INT, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_sleep_enable_timer_wakeup((uint64_t)DEEP_SLEEP_FALLBACK_S * 1000000ULL);
    esp_deep_sleep_start();
}

static void can_rx_task(void* /*param*/) {
    CANDriver     driver;
    PIDDictionary dictionary;
    bool          car_can_connected = false;

    // ── Poll scheduling ──────────────────────────────────────────────────
    // The bus is polled one PID at a time. A flat round-robin over all ~60 PIDs
    // refreshes each value only once per ~3 s (the visible client lag). Instead
    // we poll a small FAST list (the live dashboard values) every cycle and one
    // SLOW PID per cycle, so the fast PIDs refresh every (FAST_COUNT + 1) polls.
    static const uint8_t FAST_PIDS[] = {
        PID_RPM,
        PID_SPEED,
        PID_MAF,             // feeds fuel rate / consumption
        PID_ENGINE_LOAD,
    };
    static const size_t FAST_COUNT = sizeof(FAST_PIDS) / sizeof(FAST_PIDS[0]);

    // Everything else — changes slowly, rarely, or is unsupported. Drip-polled
    // one per cycle, round-robin (Mode 01 entries first, then Mode 22 DIDs).
    static const uint8_t SLOW_PIDS[] = {
        // Verified
        PID_MONITOR_STATUS,
        PID_COOLANT_TEMP,
        PID_MAP_PRESSURE,
        PID_INTAKE_AIR_TEMP,
        PID_THROTTLE,
        PID_OBD_STANDARDS,
        PID_RUNTIME,
        PID_DIST_MIL,
        PID_FUEL_RAIL_PRES,
        PID_EGR_CMD,
        PID_EGR_ERROR,
        PID_WARMUPS,
        PID_DIST_CLEARED,
        PID_BARO_PRESSURE,
        PID_CATALYST_TEMP,
        PID_MODULE_VOLTAGE,
        PID_REL_THROTTLE,
        PID_ACCEL_D,
        PID_ACCEL_E,
        PID_THROTTLE_ACT,
        PID_TIME_MIL,
        PID_TIME_CLEARED,
        // Unverified
        PID_STFT,
        PID_LTFT,
        PID_FUEL_PRESSURE,
        PID_O2_SENSOR,
        PID_ABS_LOAD,
        PID_CMD_AFR,
        PID_AMBIENT_TEMP,
        PID_THROTTLE_B,
        PID_HYBRID_BATT,
        PID_OIL_TEMP,
        PID_FUEL_RATE,
    };
    static const size_t SLOW_MODE01_COUNT = sizeof(SLOW_PIDS) / sizeof(SLOW_PIDS[0]);

    // Mode 22 poll list — 16-bit DIDs, UDS service 0x22
    // Responses arrive as 0x62 positive or 0x7F negative (currently all negative).
    static const uint16_t MODE22_POLL_PIDS[] = {
        // AT ECU (responses on 0x7E9)
        0xF100, 0xF101, 0xF102, 0xF103, 0xF104,
        0xF105, 0xF106, 0xF107, 0xF108, 0xF109, 0xF10A,
        // Engine ECU (responses on 0x7E8)
        0xF300, 0xF301, 0xF302, 0xF303, 0xF304,
        0xF305, 0xF306, 0xF307, 0xF308, 0xF309,
    };
    static const size_t MODE22_COUNT = sizeof(MODE22_POLL_PIDS) / sizeof(MODE22_POLL_PIDS[0]);

    size_t   fast_step    = 0;   // position in the FAST_COUNT-fast-then-1-slow cycle
    size_t   slow_idx     = 0;   // round-robin across SLOW_PIDS then MODE22_POLL_PIDS
    uint32_t last_poll_ms = 0;
    // Seeded to boot time so a freshly-woken board gets one CAR_OFF_TIMEOUT_MS
    // window to detect bus traffic before deciding the car is still off.
    uint32_t last_activity_ms = (uint32_t)(esp_timer_get_time() / 1000);

    Serial.println("Initializing CAN driver...");
    if (!driver.begin()) {
        Serial.println("ERROR: Failed to initialize CAN driver");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    Serial.println("TWAI initialized successfully");

    while (true) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

        // Car-off check: no CAN traffic for CAR_OFF_TIMEOUT_MS → deep sleep.
        if (now_ms - last_activity_ms > CAR_OFF_TIMEOUT_MS) {
            enterDeepSleep(driver);   // never returns
        }

        if (now_ms - last_poll_ms >= OBD_POLL_INTERVAL_MS) {
            if (fast_step < FAST_COUNT) {
                // A fast/live value — polled every cycle.
                driver.sendFrame(makeOBDRequest(FAST_PIDS[fast_step]));
            } else {
                // One slow PID this cycle, then the fast sweep restarts.
                size_t slow_total = SLOW_MODE01_COUNT + MODE22_COUNT;
                if (slow_idx < SLOW_MODE01_COUNT) {
                    driver.sendFrame(makeOBDRequest(SLOW_PIDS[slow_idx]));
                } else {
                    driver.sendFrame(makeMode22Request(MODE22_POLL_PIDS[slow_idx - SLOW_MODE01_COUNT]));
                }
                slow_idx = (slow_idx + 1) % slow_total;
            }
            fast_step    = (fast_step + 1) % (FAST_COUNT + 1);
            last_poll_ms = now_ms;
        }

        if (driver.isFrameAvailable()) {
            CANFrame frame;
            if (driver.readFrame(frame)) {
                // Any received frame (even ones we filter out below) means the
                // bus is alive → ignition is on. Refresh the activity timestamp
                // and release the broadcast task to bring up ESP-NOW.
                last_activity_ms = now_ms;
                g_car_on         = true;

                if (!car_can_connected) {
                    car_can_connected = true;
                    Serial.println("Connected to vehicle CAN bus");
                }

                // Accept responses from engine ECU (0x7E8) and AT ECU (0x7E9).
                if (frame.id != 0x7E8 && frame.id != 0x7E9) {
                    // Serial.printf("SKIP CAN ID=0x%03X DLC=%d data=%02X %02X %02X %02X %02X %02X %02X %02X\n",
                    //     frame.id, frame.dlc,
                    //     frame.data[0], frame.data[1], frame.data[2], frame.data[3],
                    //     frame.data[4], frame.data[5], frame.data[6], frame.data[7]);
                    // vTaskDelay(pdMS_TO_TICKS(1));
                    continue;
                }

                uint8_t service_response = frame.data[1];

                if (service_response == 0x41 && frame.id == 0x7E8) {
                    // --- Mode 01 positive response (engine ECU only) ---
                    uint8_t pid = frame.data[2];
                    Serial.printf("CAN ID=0x%03X PID=0x%02X DLC=%d\n", frame.id, pid, frame.dlc);

                    if (pid == PID_MONITOR_STATUS) {
                        Serial.println("  -> Updating MIL/DTC status");
                        aggregator.updateMilStatus(PIDTranslator::extractMilStatus(frame));
                        aggregator.updateDtcCount(PIDTranslator::extractDtcCount(frame));
                    } else {
                        const PidDefinition* def = dictionary.lookup(frame.id, pid);
                        if (def != nullptr) {
                            float value = PIDTranslator::translate(frame, *def);
                            Serial.printf("  -> PID 0x%02X = %.2f\n", pid, value);
                            aggregator.update(def->pid, value);
                        } else {
                            Serial.printf("  -> PID 0x%02X not in dictionary\n", pid);
                        }
                    }

                } else if (service_response == 0x62) {
                    // --- Mode 22 positive response ---
                    // Layout: [len] 0x62 [DID_high] [DID_low] [data A] [data B] ...
                    uint16_t real_pid = ((uint16_t)frame.data[2] << 8) | frame.data[3];
                    uint8_t  slot     = mode22SlotId(real_pid);
                    Serial.printf("CAN ID=0x%03X Mode22 DID=0x%04X DLC=%d\n",
                                  frame.id, real_pid, frame.dlc);
                    if (slot != 0xFF) {
                        // Store first data byte as a raw float until the formula
                        // is confirmed by the ECU documentation.
                        float value = (float)frame.data[4];
                        Serial.printf("  -> Mode22 DID=0x%04X slot=0x%02X value=%.0f\n",
                                      real_pid, slot, value);
                        aggregator.update(slot, value);
                    } else {
                        Serial.printf("  -> Mode22 DID=0x%04X not in MODE22_PID_MAP\n", real_pid);
                    }

                } else if (service_response == 0x7F) {
                    // Negative response — log and ignore
                    Serial.printf("  -> NEG_RESP CAN=0x%03X svc=0x%02X NRC=0x%02X\n",
                                  frame.id, frame.data[2], frame.data[3]);
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
    bool               broadcaster_started = false;

    while (true) {
        // Hold off bringing up the radio until the car is confirmed on. This
        // keeps timer-probe wake-ups (car still off) cheap — CAN only, no WiFi.
        if (!g_car_on) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (!broadcaster_started) {
            Serial.println("[power] car ON — starting ESP-NOW broadcaster");
            bool begin_ok = broadcaster.begin(PMK_KEY);
            Serial.printf("broadcaster.begin() returned: %d, add_peer_err=%d, send_err=%d\n",
                          begin_ok, (int)broadcaster.lastAddPeerErr(), (int)broadcaster.lastSendErr());
            broadcaster_started = true;
            last_tick_ms        = (uint32_t)(esp_timer_get_time() / 1000);
        }

        uint32_t now_ms   = (uint32_t)(esp_timer_get_time() / 1000);
        uint32_t delta_ms = now_ms - last_tick_ms;
        last_tick_ms      = now_ms;

        float speed       = aggregator.get(PID_SPEED);
        float fuel_rate   = DerivedCalculator::computeFuelRate(aggregator);
        float consumption = DerivedCalculator::computeConsumption(aggregator);

        session.update(speed, fuel_rate, delta_ms);

        Payload payload = PayloadBuilder::build(aggregator, session, consumption, now_ms);

        // Smooth altitude. OBD baro is whole-kPa, so the raw altitude snaps in
        // ~84 m steps. A slow EMA lets the baro dithering between adjacent kPa
        // values average toward the true altitude and removes the visible
        // stepping (~2 s time constant at this 10 Hz broadcast rate).
        static const float ALT_EMA_ALPHA = 0.05f;
        static bool        alt_init      = false;
        static float       alt_ema       = 0.0f;
        if (!alt_init) { alt_ema = payload.altitude_m; alt_init = true; }
        else           { alt_ema += ALT_EMA_ALPHA * (payload.altitude_m - alt_ema); }
        payload.altitude_m = alt_ema;

        bool sent = broadcaster.send(payload);
        sent ? ++send_count : ++fail_count;

        // Log every broadcast message with its full contents.
        Serial.printf(
            "[TX #%lu %s ok=%lu fail=%lu] t=%lums spd=%ukm/h rpm=%u "
            "fuel=%.1fL/h cons=%.1fkm/L avg=%.1fkm/L dist=%.1fkm alt=%.0fm "
            "load=%.0f%% volt=%.1fV boost=%.1f coolant=%.0fC flags=0x%02X\n",
            (unsigned long)send_count, sent ? "OK" : "FAIL",
            (unsigned long)send_count, (unsigned long)fail_count,
            (unsigned long)payload.timestamp_ms,
            (unsigned)payload.speed_kmh, (unsigned)payload.rpm,
            payload.fuel_rate_l_per_h, payload.consumption_km_per_l,
            payload.avg_consumption_km_per_l, payload.distance_km,
            payload.altitude_m, payload.engine_load_pct,
            payload.module_voltage_v, payload.boost_pres,
            payload.coolant_temp_c, payload.flags);

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
    delay(1500);  // Allow serial monitor to connect if present; never block when USB is absent
    Serial.println("Server starting...");

    xTaskCreatePinnedToCore(can_rx_task,    "can_rx",    4096, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(broadcast_task, "broadcast", 4096, nullptr, 3, nullptr, 0);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
#endif
