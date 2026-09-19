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
#include <math.h>
#include <sys/time.h>

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


// --- Trip persistence across deep sleep -------------------------------------
// SessionAccumulator lives on broadcast_task's stack, and enterDeepSleep()
// reboots the chip — so before this, trip distance and average consumption reset
// at every ignition-off, including a two-minute fuel stop. That made the average
// unusable for the tank-vs-receipt comparison the FUEL_RAW_TRIM calibration note
// asks for.
//
// RTC slow memory survives deep sleep (it does NOT survive a power cut or a
// reflash), so the totals live here and are re-seeded on wake. ESP-NOW is
// one-way, so no client can ask for a reset; instead the trip auto-clears once
// the car has been parked longer than TRIP_RESET_AFTER_PARK_S — short stops
// continue the trip, an overnight park starts a fresh one.
//
// Parked time is measured with gettimeofday(): IDF keeps the system clock running
// off the RTC across deep sleep, so the delta across a sleep is real elapsed time.
// The 30 s DEEP_SLEEP_FALLBACK_S probe wake-ups each add their own sleep to the
// accumulator, so a long park is counted correctly in ~30 s increments.
#define TRIP_RTC_MAGIC 0x50414A31u   // 'PAJ1' — RTC memory is garbage on cold boot
static const uint64_t TRIP_RESET_AFTER_PARK_S = 8 * 3600;   // 8 h — tune to taste

RTC_DATA_ATTR static uint32_t g_rtc_magic;
RTC_DATA_ATTR static float    g_rtc_trip_distance_km;
RTC_DATA_ATTR static float    g_rtc_trip_fuel_l;
RTC_DATA_ATTR static uint64_t g_rtc_sleep_started_us;  // gettimeofday at sleep entry
RTC_DATA_ATTR static uint64_t g_rtc_parked_us;         // accumulated off-time

static uint64_t wall_clock_us() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

// Re-seed the trip from RTC memory, clearing it on a cold boot or after a long
// park. Call once from setup(), before broadcast_task reads the values.
static void restoreTripState() {
    if (g_rtc_magic != TRIP_RTC_MAGIC) {
        // Cold boot: RTC contents are undefined. Start clean.
        g_rtc_magic            = TRIP_RTC_MAGIC;
        g_rtc_trip_distance_km = 0.0f;
        g_rtc_trip_fuel_l      = 0.0f;
        g_rtc_parked_us        = 0;
        g_rtc_sleep_started_us = 0;
        Serial.println("[trip] cold boot — trip cleared");
        return;
    }

    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED &&
        g_rtc_sleep_started_us != 0) {
        uint64_t now = wall_clock_us();
        if (now > g_rtc_sleep_started_us) {
            g_rtc_parked_us += (now - g_rtc_sleep_started_us);
        }
    }

    if (g_rtc_parked_us >= TRIP_RESET_AFTER_PARK_S * 1000000ULL) {
        Serial.printf("[trip] parked %llu min — starting a new trip\n",
                      (unsigned long long)(g_rtc_parked_us / 60000000ULL));
        g_rtc_trip_distance_km = 0.0f;
        g_rtc_trip_fuel_l      = 0.0f;
        g_rtc_parked_us        = 0;
    } else {
        Serial.printf("[trip] resuming: %.1f km, %.2f L (parked %llu min)\n",
                      g_rtc_trip_distance_km, g_rtc_trip_fuel_l,
                      (unsigned long long)(g_rtc_parked_us / 60000000ULL));
    }
}

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

// Build a Mode 22 (UDS ReadDataByIdentifier) request for a 16-bit DID, sent with
// physical addressing to a specific ECU (req_id). The response arrives on
// req_id + UDS_RESP_OFFSET as an ISO-TP single or multi frame.
static CANFrame makeMode22Request(uint16_t req_id, uint16_t did) {
    CANFrame f = {};
    f.id          = req_id;
    f.dlc         = 8;
    f.is_extended = false;
    f.data[0]     = 0x03;                 // ISO-TP single frame, length 3
    f.data[1]     = 0x22;                 // UDS ReadDataByIdentifier
    f.data[2]     = (did >> 8) & 0xFF;
    f.data[3]     = did & 0xFF;
    return f;
}

// Build an ISO-TP flow-control "clear to send" frame (block size 0, ST 0).
static CANFrame makeFlowControl(uint16_t req_id) {
    CANFrame f = {};
    f.id          = req_id;
    f.dlc         = 8;
    f.is_extended = false;
    f.data[0]     = 0x30;                 // FC, ContinueToSend
    return f;
}

// Apply every advanced-PID formula keyed to this (ECU, DID) against a fully
// reassembled UDS positive response and push the results into the aggregator.
static void dispatchMode22(uint32_t resp_id, const uint8_t* uds, uint8_t uds_len,
                           DataAggregator& agg) {
    if (uds_len < 3 || uds[0] != 0x62) return;
    uint16_t did    = ((uint16_t)uds[1] << 8) | uds[2];
    uint16_t req_id = (uint16_t)(resp_id - UDS_RESP_OFFSET);
    for (size_t i = 0; i < MODE22_ADVANCED_PIDS_SIZE; ++i) {
        const Mode22AdvancedPid& def = MODE22_ADVANCED_PIDS[i];
        if (def.req_id != req_id || def.did != did) continue;
        float v = PIDTranslator::translateMode22(uds, uds_len, def);
        if (!isnan(v)) {
            agg.update(def.slot_id, v);
            Serial.printf("  -> M22 %s = %.1f %s (DID 0x%04X)\n",
                          def.name, v, def.unit, did);
        }
    }
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

    // Stamp the clock so restoreTripState() can measure how long this park lasts.
    g_rtc_sleep_started_us = wall_clock_us();

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
        PID_ACCEL_D,         // overrun fuel-cut detection needs a live pedal — a
        PID_ACCEL_E,         // stale (slow-polled) pedal misses the lift-off window
        PID_MAP_PRESSURE,    // boost = MAP - baro; a slow-polled MAP lagged the gauge ~11 s
    };
    static const size_t FAST_COUNT = sizeof(FAST_PIDS) / sizeof(FAST_PIDS[0]);

    // Everything else — changes slowly, rarely, or is unsupported. Drip-polled
    // one per cycle, round-robin (Mode 01 entries first, then Mode 22 DIDs).
    static const uint8_t SLOW_PIDS[] = {
        // Verified
        PID_MONITOR_STATUS,
        PID_COOLANT_TEMP,
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
        PID_OIL_TEMP,
        PID_FUEL_RATE,
    };
    static const size_t SLOW_MODE01_COUNT = sizeof(SLOW_PIDS) / sizeof(SLOW_PIDS[0]);

    // Mode 22 poll list — the Mitsubishi advanced PIDs from the reverse-eng doc.
    // DISABLED: a sniffer DID sweep (projects/sniffer) confirmed this vehicle's
    // ECUs do not implement UDS service 0x22 — the engine answers every DID with
    // 0x11 (serviceNotSupported) and the TCM with 0x80. Polling them only wasted
    // a slow-round-robin slot per cycle on guaranteed rejections, slowing the
    // genuinely-useful slow PIDs. Kept here (gated off) for reference and for a
    // future vehicle that does support them; the ISO-TP reassembly path below
    // stays in place, dormant, since nothing requests these DIDs.
    static constexpr bool POLL_MODE22 = false;
    struct Mode22Req { uint16_t req_id; uint16_t did; };
    static const Mode22Req MODE22_REQS[] = {
        { UDS_REQ_ENGINE, 0x20F2 },   // fuel temperature
        { UDS_REQ_ENGINE, 0x2151 },   // cooling fan duty
        { UDS_REQ_TCM,    0x20AB },   // transmission input + output speed
    };
    static const size_t MODE22_COUNT = sizeof(MODE22_REQS) / sizeof(MODE22_REQS[0]);

    size_t   fast_step    = 0;   // position in the FAST_COUNT-fast-then-1-slow cycle
    size_t   slow_idx     = 0;   // round-robin across SLOW_PIDS then MODE22_POLL_PIDS
    uint32_t last_poll_ms = 0;
    // Seeded to boot time so a freshly-woken board gets one CAR_OFF_TIMEOUT_MS
    // window to detect bus traffic before deciding the car is still off.
    uint32_t last_activity_ms = (uint32_t)(esp_timer_get_time() / 1000);

    // ISO-TP reassembly state — one slot per advanced-PID ECU response id.
    // Mode 22 advanced PIDs return data bytes that spill past the 7-byte single
    // frame (e.g. fuel temp at D4), so we must reassemble multi-frame responses.
    struct IsoTpRx {
        uint32_t resp_id;
        bool     active;
        uint16_t total;      // expected reassembled UDS length
        uint16_t got;        // bytes collected so far
        uint8_t  buf[64];    // reassembled UDS, buf[0] == service byte (0x62)
    };
    IsoTpRx isotp[2] = {
        { 0x7E8, false, 0, 0, {0} },   // engine ECU
        { 0x7E9, false, 0, 0, {0} },   // transmission ECU
    };

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
        // Drive the aggregator's clock so every stored value carries an accurate
        // arrival time and DerivedCalculator's isFresh() guards mean something.
        aggregator.setNow(now_ms);

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
                size_t slow_total = SLOW_MODE01_COUNT + (POLL_MODE22 ? MODE22_COUNT : 0);
                if (slow_idx < SLOW_MODE01_COUNT) {
                    driver.sendFrame(makeOBDRequest(SLOW_PIDS[slow_idx]));
                } else {
                    const Mode22Req& r = MODE22_REQS[slow_idx - SLOW_MODE01_COUNT];
                    driver.sendFrame(makeMode22Request(r.req_id, r.did));
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
                if (!g_car_on) {
                    // Ignition just came back on: whatever park we were counting
                    // ended without crossing the reset threshold, so this trip
                    // continues and the accumulator starts fresh.
                    g_rtc_parked_us = 0;
                }
                g_car_on         = true;

                if (!car_can_connected) {
                    car_can_connected = true;
                    Serial.println("Connected to vehicle CAN bus");
                }

                // --- Free-running broadcast frames (passive; no request sent) ---
                // The 4M41 broadcasts injected fuel (0x608) and gear (0x218)
                // continuously. Capture them here before the request/response
                // filter below. See projects/sniffer/assets/MITSUBISHI_ADVANCED_PIDS.md.
                if (frame.id == CAN_BCAST_FUEL && frame.dlc >= 7) {
                    float raw = (float)(((uint16_t)frame.data[5] << 8) | frame.data[6]);
                    aggregator.update(PID_BCAST_FUEL_RAW, raw);
                    continue;
                }
                if (frame.id == CAN_BCAST_GEAR && frame.dlc >= 3) {
                    aggregator.update(PID_M22_AT_GEAR_POS,    (float)(frame.data[2] & 0x0F));
                    aggregator.update(PID_M22_AT_TARGET_GEAR, (float)(frame.data[2] >> 4));
                    continue;
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

                uint8_t pci = frame.data[0] & 0xF0;

                if (pci == 0x00) {
                    // ── ISO-TP single frame ──────────────────────────────────
                    uint8_t service_response = frame.data[1];

                    if (service_response == 0x41 && frame.id == 0x7E8) {
                        // --- Mode 01 positive response (engine ECU only) ---
                        uint8_t pid = frame.data[2];

                        if (pid == PID_MONITOR_STATUS) {
                            aggregator.updateMilStatus(PIDTranslator::extractMilStatus(frame));
                            aggregator.updateDtcCount(PIDTranslator::extractDtcCount(frame));
                        } else {
                            const PidDefinition* def = dictionary.lookup(frame.id, pid);
                            if (def != nullptr) {
                                float value = PIDTranslator::translate(frame, *def);
                                aggregator.update(def->pid, value);
                            }
                        }

                    } else if (service_response == 0x62) {
                        // --- Mode 22 positive response, single frame ---
                        // UDS = frame.data[1 .. 1+len]; buf[0]=0x62, buf[1..2]=DID.
                        uint8_t len = frame.data[0] & 0x0F;
                        uint8_t uds[8] = {0};
                        for (uint8_t k = 0; k < len && (uint8_t)(1 + k) < 8; ++k) {
                            uds[k] = frame.data[1 + k];
                        }
                        dispatchMode22(frame.id, uds, len, aggregator);

                    } else if (service_response == 0x7F) {
                        // Negative response — ignore
                    }

                } else if (pci == 0x10) {
                    // ── ISO-TP first frame: start reassembly, send flow control ─
                    IsoTpRx& rx = (frame.id == 0x7E8) ? isotp[0] : isotp[1];
                    rx.total  = (((uint16_t)(frame.data[0] & 0x0F)) << 8) | frame.data[1];
                    rx.got    = 0;
                    rx.active = true;
                    // First frame carries 6 UDS bytes (data[2..7]).
                    for (uint8_t k = 0; k < 6 && rx.got < rx.total && rx.got < sizeof(rx.buf); ++k) {
                        rx.buf[rx.got++] = frame.data[2 + k];
                    }
                    driver.sendFrame(makeFlowControl(frame.id - UDS_RESP_OFFSET));

                } else if (pci == 0x20) {
                    // ── ISO-TP consecutive frame: append until complete ───────
                    IsoTpRx& rx = (frame.id == 0x7E8) ? isotp[0] : isotp[1];
                    if (rx.active) {
                        for (uint8_t k = 0; k < 7 && rx.got < rx.total && rx.got < sizeof(rx.buf); ++k) {
                            rx.buf[rx.got++] = frame.data[1 + k];
                        }
                        if (rx.got >= rx.total) {
                            uint8_t uds_len = (rx.total < sizeof(rx.buf))
                                              ? (uint8_t)rx.total : (uint8_t)sizeof(rx.buf);
                            dispatchMode22(frame.id, rx.buf, uds_len, aggregator);
                            rx.active = false;
                        }
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
    session.restore(g_rtc_trip_distance_km, g_rtc_trip_fuel_l);
    ESPNowBroadcaster  broadcaster;
    uint32_t           last_tick_ms = 0;
    uint32_t           send_count = 0;
    uint32_t           fail_count = 0;
    uint8_t            log_divider = 0;   // throttles the default (non-VERBOSE_TX) log
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
            // Printed so it can be pasted into each client's security_config.h as
            // ESPNOW_SERVER_MAC — the broadcast is unencrypted and unauthenticated,
            // so pinning this MAC is what keeps a stranger's frames off the dash.
            Serial.printf("[espnow] server station MAC: %s\n",
                          WiFi.macAddress().c_str());
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
        // Mirror into RTC on every tick: enterDeepSleep() is called from
        // can_rx_task and never returns, so there is no shutdown hook to flush
        // from. Two word writes at 10 Hz into RTC RAM cost nothing (no flash).
        g_rtc_trip_distance_km = session.getDistanceKm();
        g_rtc_trip_fuel_l      = session.getTotalFuelL();

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

        // Per-broadcast logging.
        //
        // The full dump below is ~900 characters. At the 10 Hz broadcast rate that
        // is ~9 KB/s against the 11.5 KB/s a 115200 line can carry, so sustained it
        // very nearly saturates the link — and on USB-CDC a write stalls once the
        // TX buffer fills, quietly holding this loop below 10 Hz. It exists to
        // calibrate DerivedCalculator against a real drive (its constants document
        // tuning "against the server log"), so it stays — behind a flag.
        //
        // Build with -DVERBOSE_TX for a calibration drive; the default is one
        // compact line per second carrying what the calibration knobs need.
#ifdef VERBOSE_TX
        Serial.printf(
            "[TX #%lu %s ok=%lu fail=%lu] ver=%u t=%lums\n"
            "  rpm=%u spd=%ukm/h fuel_rate=%.2fL/h cons=%.2fkm/L avg=%.2fkm/L dist=%.2fkm\n"
            "  mil=%d dtc=%u flags=0x%02X\n"
            "  load=%.1f%% coolant=%.1fC map=%ukPa iat=%.1fC maf=%.2fg/s throttle=%.1f%%\n"
            "  runtime=%us dist_mil=%ukm fuel_rail=%.1fkPa egr_cmd=%.1f%% egr_err=%.1f%%\n"
            "  warmups=%u dist_cleared=%ukm baro=%ukPa alt=%.1fm cat=%.1fC volt=%.2fV\n"
            "  rel_thr=%.1f%% accel_d=%.1f%% accel_e=%.1f%% thr_act=%.1f%% time_mil=%umin time_cleared=%umin\n"
            "  stft=%.1f%% ltft=%.1f%% fuel_pres=%.1fkPa o2=%.3f abs_load=%.1f%% cmd_afr=%.3f\n"
            "  ambient=%.1fC throttle_b=%.1f%% oil=%.1fC obd_std=%u\n"
            "  gear=%.0f tgt_gear=%.0f boost=%.2fbar\n",
            (unsigned long)send_count, sent ? "OK" : "FAIL",
            (unsigned long)send_count, (unsigned long)fail_count,
            (unsigned)payload.version, (unsigned long)payload.timestamp_ms,
            (unsigned)payload.rpm, (unsigned)payload.speed_kmh,
            payload.fuel_rate_l_per_h, payload.consumption_km_per_l,
            payload.avg_consumption_km_per_l, payload.distance_km,
            (int)payload.mil_on, (unsigned)payload.dtc_count, payload.flags,
            payload.engine_load_pct, payload.coolant_temp_c, (unsigned)payload.map_pressure_kpa,
            payload.intake_air_temp_c, payload.maf_g_per_s, payload.throttle_pct,
            (unsigned)payload.runtime_s, (unsigned)payload.dist_mil_km, payload.fuel_rail_pres_kpa,
            payload.egr_cmd_pct, payload.egr_error_pct,
            (unsigned)payload.warmups, (unsigned)payload.dist_cleared_km,
            (unsigned)payload.baro_pressure_kpa, payload.altitude_m, payload.catalyst_temp_c,
            payload.module_voltage_v,
            payload.rel_throttle_pct, payload.accel_d_pct, payload.accel_e_pct, payload.throttle_act_pct,
            (unsigned)payload.time_mil_min, (unsigned)payload.time_cleared_min,
            payload.stft_pct, payload.ltft_pct, payload.fuel_pressure_kpa, payload.o2_sensor,
            payload.abs_load_pct, payload.cmd_afr_lambda,
            payload.ambient_temp_c, payload.throttle_b_pct,
            payload.oil_temp_c, (unsigned)payload.obd_standards,
            payload.at_gear_pos, payload.at_target_gear, payload.boost_pres);
#else
        // One line per second (~110 chars → ~0.1 KB/s), carrying exactly the
        // fields the DerivedCalculator constants are tuned against: maf, load and
        // the resulting fuel rate / economy.
        if (++log_divider >= 10) {
            log_divider = 0;
            Serial.printf(
                "[TX #%lu %s fail=%lu] rpm=%u spd=%u load=%.0f%% maf=%.1f "
                "fuel=%.2fL/h cons=%.1f avg=%.1f dist=%.1fkm boost=%.2f flags=0x%02X\n",
                (unsigned long)send_count, sent ? "OK" : "FAIL", (unsigned long)fail_count,
                (unsigned)payload.rpm, (unsigned)payload.speed_kmh,
                payload.engine_load_pct, payload.maf_g_per_s,
                payload.fuel_rate_l_per_h, payload.consumption_km_per_l,
                payload.avg_consumption_km_per_l, payload.distance_km,
                payload.boost_pres, payload.flags);
        }
#endif

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

    restoreTripState();

    xTaskCreatePinnedToCore(can_rx_task,    "can_rx",    4096, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(broadcast_task, "broadcast", 4096, nullptr, 3, nullptr, 0);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
#endif
