#pragma once

#include <stdint.h>
#include <stdbool.h>

#define PAYLOAD_VERSION 5

// Wire budget. ESP-NOW caps a single broadcast at 250 bytes, so every field here
// is spent out of a fixed allowance. Only fields some code path can actually
// WRITE belong in this struct: a field nothing populates still costs its width on
// every one of the 10 broadcasts per second. See the "Dead weight" note below.
#define PAYLOAD_MAX_WIRE_BYTES 250

typedef struct __attribute__((packed)) {
    uint8_t  version;
    uint32_t timestamp_ms;

    // --- Core driving data ---
    uint16_t rpm;                       // PID_RPM        (0x0C)  rpm
    uint8_t  speed_kmh;                 // PID_SPEED       (0x0D)  km/h
    float    fuel_rate_l_per_h;         // PID_FUEL_RATE   (0x5E)  L/h
    float    consumption_km_per_l;      // derived
    float    avg_consumption_km_per_l;  // session derived
    float    distance_km;              // session derived

    // --- MIL / DTC (from PID_MONITOR_STATUS 0x01) ---
    bool     mil_on;
    uint8_t  dtc_count;

    // --- Verified PIDs ---
    float    engine_load_pct;           // PID_ENGINE_LOAD   (0x04)  %
    float    coolant_temp_c;            // PID_COOLANT_TEMP  (0x05)  °C
    uint8_t  map_pressure_kpa;          // PID_MAP_PRESSURE  (0x0B)  kPa
    float    intake_air_temp_c;         // PID_INTAKE_AIR_TEMP (0x0F) °C
    float    maf_g_per_s;               // PID_MAF           (0x10)  g/s
    float    throttle_pct;              // PID_THROTTLE      (0x11)  %
    uint16_t runtime_s;                 // PID_RUNTIME       (0x1F)  s
    uint16_t dist_mil_km;               // PID_DIST_MIL      (0x21)  km
    float    fuel_rail_pres_kpa;        // PID_FUEL_RAIL_PRES (0x23) kPa
    float    egr_cmd_pct;               // PID_EGR_CMD       (0x2C)  %
    float    egr_error_pct;             // PID_EGR_ERROR     (0x2D)  %
    uint8_t  warmups;                   // PID_WARMUPS       (0x30)  count
    uint16_t dist_cleared_km;           // PID_DIST_CLEARED  (0x31)  km
    uint8_t  baro_pressure_kpa;         // PID_BARO_PRESSURE (0x33)  kPa
    float    altitude_m;                // derived from baro_pressure_kpa  m
    float    catalyst_temp_c;           // PID_CATALYST_TEMP (0x3C)  °C
    float    module_voltage_v;          // PID_MODULE_VOLTAGE (0x42) V
    float    rel_throttle_pct;          // PID_REL_THROTTLE  (0x45)  %
    float    accel_d_pct;               // PID_ACCEL_D       (0x49)  %
    float    accel_e_pct;               // PID_ACCEL_E       (0x4A)  %
    float    throttle_act_pct;          // PID_THROTTLE_ACT  (0x4C)  %
    uint16_t time_mil_min;              // PID_TIME_MIL      (0x4D)  min
    uint16_t time_cleared_min;          // PID_TIME_CLEARED  (0x4E)  min

    // --- Unverified PIDs (polled, but this ECU may reject them → 0) ---
    // These stay in the payload because the server DOES request them: if the ECU
    // ever answers, the value lands here. That is the line for membership in this
    // struct — a poll exists, so a value can exist.
    float    stft_pct;                  // PID_STFT          (0x06)  %
    float    ltft_pct;                  // PID_LTFT          (0x07)  %
    float    fuel_pressure_kpa;         // PID_FUEL_PRESSURE (0x0A)  kPa
    float    o2_sensor;                 // PID_O2_SENSOR     (0x24)  complex
    float    abs_load_pct;              // PID_ABS_LOAD      (0x43)  %
    float    cmd_afr_lambda;            // PID_CMD_AFR       (0x44)  lambda
    float    ambient_temp_c;            // PID_AMBIENT_TEMP  (0x46)  °C
    float    throttle_b_pct;            // PID_THROTTLE_B    (0x47)  %
    float    oil_temp_c;                // PID_OIL_TEMP      (0x5C)  °C

    // --- Mode 01 informational ---
    uint8_t  obd_standards;             // PID_OBD_STANDARDS (0x1C)  raw enum (6=EOBD)

    // --- Free-running CAN broadcast frames (passive; nothing is requested) ---
    // Gear comes from CAN 0x218 D2, which the 4M41 emits continuously. Codes:
    //   0=N, 1..5=forward, 0xB(11)=R, 0xD(13)=P
    float    at_gear_pos;               // 0x218 D2 low nibble  — current gear
    float    at_target_gear;            // 0x218 D2 high nibble — target gear

    // --- Derived ---
    float    boost_pres;                // bar (gauge) = MAP - ambient, computed
                                        // server-side by DerivedCalculator

    uint8_t  flags;
} Payload;

#define PAYLOAD_FLAG_DATA_VALID     (1 << 0)
#define PAYLOAD_FLAG_ENGINE_RUNNING (1 << 1)

static_assert(sizeof(Payload) == 149,
    "Payload size mismatch - check struct fields and packing");
static_assert(sizeof(Payload) <= PAYLOAD_MAX_WIRE_BYTES,
    "Payload exceeds the 250-byte ESP-NOW broadcast limit");

// --- Dead weight removed in PAYLOAD_VERSION 5 --------------------------------
//
// v4 was 233 bytes, of which 84 could never be anything but zero on this vehicle.
// A sniffer DID sweep (projects/sniffer) established that these ECUs do not
// implement UDS service 0x22 at all — the engine answers every DID with 0x11
// (serviceNotSupported) and the TCM with 0x80 — so projects/server/src/main.cpp
// sets POLL_MODE22 = false and never sends a Mode 22 request. Nothing requests
// them, so nothing can ever dispatch a response into their aggregator slots.
//
// Removed (21 floats, 84 bytes):
//   AT ECU      at_gear_ratio, at_input_speed_rpm, at_output_speed_rpm,
//               at_tc_slip_rpm, at_atf_temp_c, at_shift_sol_status,
//               at_lockup_status, at_prndl, at_oil_pres
//   Engine 0xF3xx  egr_valve_pos_pct, dpf_soot_load, dpf_regen_status,
//               rail_pres_act, rail_pres_des, inj_cor_cyl1..4
//   Mitsubishi  fuel_temp_c (DID 0x20F2), cooling_fan_duty_pct (DID 0x2151)
//   Other       hybrid_batt_pct — a 4M41 diesel has no hybrid battery; the poll
//               was also dropped from SLOW_PIDS, since it only ever bought a
//               guaranteed rejection in a round-robin slot.
//
// NOTE: earlier revisions of this header claimed the shaft speeds were "read from
// the real Mitsubishi advanced DID 0x20AB (TCM)". They were not, and could not be
// — see above. The claim is gone rather than corrected in place.
//
// The PID_M22_* slot IDs in pid_map.h, the MODE22_ADVANCED_PIDS[] table and the
// ISO-TP reassembly path in main.cpp are all deliberately KEPT, dormant. They cost
// nothing on the wire and are what a future vehicle (or a newly found DID) would
// need. Reviving one means re-adding its Payload field and bumping the version.
//
// If the remaining fields ever need to shrink further, the next move is encoding
// the 0.1-precision floats as scaled uint16_t, which roughly halves what is left
// at the cost of touching every client's read path.
