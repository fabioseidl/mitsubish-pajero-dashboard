#pragma once

#include <stdint.h>
#include <stdbool.h>

#define PAYLOAD_VERSION 3

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

    // --- Unverified PIDs (may be 0 if unsupported by vehicle) ---
    float    stft_pct;                  // PID_STFT          (0x06)  %
    float    ltft_pct;                  // PID_LTFT          (0x07)  %
    float    fuel_pressure_kpa;         // PID_FUEL_PRESSURE (0x0A)  kPa
    float    o2_sensor;                 // PID_O2_SENSOR     (0x24)  complex
    float    abs_load_pct;              // PID_ABS_LOAD      (0x43)  %
    float    cmd_afr_lambda;            // PID_CMD_AFR       (0x44)  lambda
    float    ambient_temp_c;            // PID_AMBIENT_TEMP  (0x46)  °C
    float    throttle_b_pct;            // PID_THROTTLE_B    (0x47)  %
    float    hybrid_batt_pct;           // PID_HYBRID_BATT   (0x5B)  %
    float    oil_temp_c;                // PID_OIL_TEMP      (0x5C)  °C

    // --- Mode 01 informational ---
    uint8_t  obd_standards;             // PID_OBD_STANDARDS (0x1C)  raw enum (6=EOBD)

    // --- Mode 22 — AT ECU (0x7E9), real PIDs 0xF100–0xF10A ---
    // All returned negative responses during initial scan; values are 0 until
    // the ECU grants access.  Formulas and units are unconfirmed.
    float    at_gear_pos;               // F100  current gear position
    float    at_gear_ratio;             // F101  gear ratio
    float    at_input_speed_rpm;        // F102  input shaft speed  (rpm)
    float    at_output_speed_rpm;       // F103  output shaft speed (rpm)
    float    at_tc_slip_rpm;            // F104  torque converter slip (rpm)
    float    at_atf_temp_c;             // F105  ATF temperature (°C)
    float    at_shift_sol_status;       // F106  shift solenoid status (bitmask)
    float    at_lockup_status;          // F107  lock-up engaged (0/1)
    float    at_prndl;                  // F108  gear selector position (PRNDL)
    float    at_target_gear;            // F109  target gear
    float    at_oil_pres;               // F10A  transmission oil pressure

    // --- Mode 22 — Engine ECU (0x7E8), real PIDs 0xF300–0xF309 ---
    float    boost_pres;                // F300  boost pressure (turbo)
    float    egr_valve_pos_pct;         // F301  EGR valve position
    float    dpf_soot_load;             // F302  DPF soot load
    float    dpf_regen_status;          // F303  DPF regeneration status
    float    rail_pres_act;             // F304  fuel rail pressure — actual
    float    rail_pres_des;             // F305  fuel rail pressure — desired
    float    inj_cor_cyl1;              // F306  injector correction cyl 1
    float    inj_cor_cyl2;              // F307  injector correction cyl 2
    float    inj_cor_cyl3;              // F308  injector correction cyl 3
    float    inj_cor_cyl4;              // F309  injector correction cyl 4

    uint8_t  flags;
} Payload;

#define PAYLOAD_FLAG_DATA_VALID     (1 << 0)
#define PAYLOAD_FLAG_ENGINE_RUNNING (1 << 1)

static_assert(sizeof(Payload) == 225,
    "Payload size mismatch - check struct fields and packing");
