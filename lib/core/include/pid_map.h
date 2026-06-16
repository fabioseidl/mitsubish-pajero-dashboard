#pragma once
#include <stdint.h>
#include <stddef.h>

typedef enum {
    FORMULA_LINEAR,
    FORMULA_BITMASK,
    FORMULA_COMPLEX,
} FormulaType;

typedef struct {
    uint16_t    pid;
    const char* name;
    const char* unit;
    uint8_t     data_bytes;
    FormulaType formula_type;
    float       a_mult;
    float       b_mult;
    float       scale;
    float       offset;
    float       min_value;
    float       max_value;
    bool        verified;
} PidDefinition;

// ---------- Mode 01 — Verified PIDs ----------
#define PID_MONITOR_STATUS  0x01u
#define PID_ENGINE_LOAD     0x04u
#define PID_COOLANT_TEMP    0x05u
#define PID_MAP_PRESSURE    0x0Bu
#define PID_RPM             0x0Cu
#define PID_SPEED           0x0Du
#define PID_INTAKE_AIR_TEMP 0x0Fu
#define PID_MAF             0x10u
#define PID_THROTTLE        0x11u
#define PID_OBD_STANDARDS   0x1Cu  // raw byte: 1=OBD-II, 6=EOBD, etc.
#define PID_RUNTIME         0x1Fu
#define PID_DIST_MIL        0x21u
#define PID_FUEL_RAIL_PRES  0x23u
#define PID_EGR_CMD         0x2Cu
#define PID_EGR_ERROR       0x2Du
#define PID_WARMUPS         0x30u
#define PID_DIST_CLEARED    0x31u
#define PID_BARO_PRESSURE   0x33u
#define PID_CATALYST_TEMP   0x3Cu
#define PID_MODULE_VOLTAGE  0x42u
#define PID_REL_THROTTLE    0x45u
#define PID_ACCEL_D         0x49u
#define PID_ACCEL_E         0x4Au
#define PID_THROTTLE_ACT    0x4Cu
#define PID_TIME_MIL        0x4Du
#define PID_TIME_CLEARED    0x4Eu

// ---------- Mode 01 — Unverified PIDs ----------
#define PID_STFT            0x06u
#define PID_LTFT            0x07u
#define PID_FUEL_PRESSURE   0x0Au
#define PID_O2_SENSOR       0x24u
#define PID_ABS_LOAD        0x43u
#define PID_CMD_AFR         0x44u
#define PID_AMBIENT_TEMP    0x46u
#define PID_THROTTLE_B      0x47u
#define PID_HYBRID_BATT     0x5Bu
#define PID_OIL_TEMP        0x5Cu
#define PID_FUEL_RATE       0x5Eu

// ---------- Mode 22 — DataAggregator slot IDs ----------
// Mode 22 DIDs (0x20xx / 0x21xx) exceed the DataAggregator's uint8-indexed
// store.  Each is remapped to a slot ID in the 0xA0–0xBF range, which is unused
// by all Mode 01 PIDs.  The server's Mode 22 response handler looks the slot up
// via MODE22_ADVANCED_PIDS below.
//
// The 0xA0–0xAA / 0xB0–0xB9 ranges below were originally reserved for a set of
// speculative DIDs (0xF1xx/0xF3xx) that the ECU never answered.  They are kept
// for payload-field continuity; only the slots referenced by
// MODE22_ADVANCED_PIDS are actually populated from the bus.
//
// AT ECU (CAN response 0x7E9) — real PIDs 0xF100–0xF10A
#define PID_M22_AT_GEAR_POS      0xA0u  // Current gear position
#define PID_M22_AT_GEAR_RATIO    0xA1u  // Gear ratio
#define PID_M22_AT_INPUT_SPEED   0xA2u  // Input shaft speed  (rpm)
#define PID_M22_AT_OUTPUT_SPEED  0xA3u  // Output shaft speed (rpm)
#define PID_M22_AT_TC_SLIP       0xA4u  // Torque converter slip (rpm)
#define PID_M22_AT_ATF_TEMP      0xA5u  // ATF temperature (°C)
#define PID_M22_AT_SHIFT_SOL     0xA6u  // Shift solenoid status (bitmask)
#define PID_M22_AT_LOCKUP        0xA7u  // Lock-up status
#define PID_M22_AT_PRNDL         0xA8u  // Gear selector position (PRNDL)
#define PID_M22_AT_TARGET_GEAR   0xA9u  // Target gear
#define PID_M22_AT_OIL_PRES      0xAAu  // Transmission oil pressure
//
// Engine ECU (CAN response 0x7E8) — real PIDs 0xF300–0xF309
#define PID_M22_BOOST_PRES       0xB0u  // Boost pressure (turbo)
#define PID_M22_EGR_VALVE_POS    0xB1u  // EGR valve position
#define PID_M22_DPF_SOOT         0xB2u  // DPF soot load
#define PID_M22_DPF_REGEN        0xB3u  // DPF regeneration status
#define PID_M22_RAIL_PRES_ACT    0xB4u  // Fuel rail pressure — actual
#define PID_M22_RAIL_PRES_DES    0xB5u  // Fuel rail pressure — desired
#define PID_M22_INJ_COR_CYL1     0xB6u  // Injector correction cyl 1
#define PID_M22_INJ_COR_CYL2     0xB7u  // Injector correction cyl 2
#define PID_M22_INJ_COR_CYL3     0xB8u  // Injector correction cyl 3
#define PID_M22_INJ_COR_CYL4     0xB9u  // Injector correction cyl 4
//
// Mitsubishi advanced PIDs confirmed for the Pajero IV 3.2 DI-D (4M41)
// (see projects/sniffer/assets/MITSUBISHI_ADVANCED_PIDS.md)
#define PID_M22_FUEL_TEMP        0xBAu  // Fuel temperature (°C), DID 0x20F2
#define PID_M22_FAN_DUTY         0xBBu  // Cooling fan duty (%),  DID 0x2151

// ---------- Free-running broadcast frames (no request / response) ----------
// Confirmed on the Pajero IV 4M41 bus by passive capture (projects/sniffer):
//   CAN 0x608 — injected fuel quantity in D5,D6 (raw 16-bit); reads 0 on overrun.
//   CAN 0x218 — gear in D2: low nibble = current gear, high nibble = target.
//               Codes: 0x0=N, 0x1..0x5 = forward gears, 0xB=R, 0xD=P.
// The gear feeds the existing AT slots PID_M22_AT_GEAR_POS / _TARGET_GEAR; the
// fuel quantity gets its own slot below and drives DerivedCalculator.
#define CAN_BCAST_FUEL   0x608u
#define CAN_BCAST_GEAR   0x218u
#define PID_BCAST_FUEL_RAW       0xBCu  // CAN 0x608 (D5<<8|D6), raw injected fuel

// 0x218 gear codes (one nibble of D2). Forward gears are their own value 1..5;
// the non-driving states use these. Shared by the server decode, the emulator
// and clients so they all agree on the encoding carried in at_gear_pos.
#define GEAR_CODE_NEUTRAL  0x0u
#define GEAR_CODE_REVERSE  0xBu
#define GEAR_CODE_PARK     0xDu

static const PidDefinition PID_MAP[] = {
    { PID_MONITOR_STATUS,  "Monitor Status",                   "",      4,     FORMULA_BITMASK, 0.0f,    0.0f,    0.0f,           0.0f,     0.0f,      0.0f,      true  },
    { PID_ENGINE_LOAD,     "Calculated Engine Load",           "%",     1,     FORMULA_LINEAR,  1.0f,    0.0f,    0.392157f,      0.0f,     0.0f,      100.0f,    true  },
    { PID_COOLANT_TEMP,    "Engine Coolant Temperature",       "C",     1,     FORMULA_LINEAR,  1.0f,    0.0f,    1.0f,           -40.0f,   -40.0f,    215.0f,    true  },
    { PID_MAP_PRESSURE,    "Intake Manifold Pressure",         "kPa",   1,     FORMULA_LINEAR,  1.0f,    0.0f,    1.0f,           0.0f,     0.0f,      255.0f,    true  },
    { PID_RPM,             "Engine RPM",                       "rpm",   2,     FORMULA_LINEAR,  256.0f,  1.0f,    0.25f,          0.0f,     0.0f,      16383.75f, true  },
    { PID_SPEED,           "Vehicle Speed",                    "km/h",  1,     FORMULA_LINEAR,  1.0f,    0.0f,    1.0f,           0.0f,     0.0f,      255.0f,    true  },
    { PID_INTAKE_AIR_TEMP, "Intake Air Temperature",           "C",     1,     FORMULA_LINEAR,  1.0f,    0.0f,    1.0f,           -40.0f,   -40.0f,    215.0f,    true  },
    { PID_MAF,             "MAF Air Flow Rate",                "g/s",   2,     FORMULA_LINEAR,  256.0f,  1.0f,    0.01f,          0.0f,     0.0f,      655.35f,   true  },
    { PID_THROTTLE,        "Throttle Position",                "%",     1,     FORMULA_LINEAR,  1.0f,    0.0f,    0.392157f,      0.0f,     0.0f,      100.0f,    true  },
    { PID_OBD_STANDARDS,   "OBD Standards",                    "",      1,     FORMULA_LINEAR,  1.0f,    0.0f,    1.0f,           0.0f,     0.0f,      255.0f,    true  },
    { PID_RUNTIME,         "Run Time Since Engine Start",      "s",     2,     FORMULA_LINEAR,  256.0f,  1.0f,    1.0f,           0.0f,     0.0f,      65535.0f,  true  },
    { PID_DIST_MIL,        "Distance Traveled with MIL On",   "km",    2,     FORMULA_LINEAR,  256.0f,  1.0f,    1.0f,           0.0f,     0.0f,      65535.0f,  true  },
    { PID_FUEL_RAIL_PRES,  "Fuel Rail Gauge Pressure",         "kPa",   2,     FORMULA_LINEAR,  256.0f,  1.0f,    10.0f,          0.0f,     0.0f,      655350.0f, true  },
    { PID_EGR_CMD,         "Commanded EGR",                    "%",     1,     FORMULA_LINEAR,  1.0f,    0.0f,    0.392157f,      0.0f,     0.0f,      100.0f,    true  },
    { PID_EGR_ERROR,       "EGR Error",                        "%",     1,     FORMULA_LINEAR,  1.0f,    0.0f,    0.78125f,       -100.0f,  -100.0f,   99.2f,     true  },
    { PID_WARMUPS,         "Warm-ups Since Codes Cleared",     "count", 1,     FORMULA_LINEAR,  1.0f,    0.0f,    1.0f,           0.0f,     0.0f,      255.0f,    true  },
    { PID_DIST_CLEARED,    "Distance Since Codes Cleared",     "km",    2,     FORMULA_LINEAR,  256.0f,  1.0f,    1.0f,           0.0f,     0.0f,      65535.0f,  true  },
    { PID_BARO_PRESSURE,   "Absolute Barometric Pressure",     "kPa",   1,     FORMULA_LINEAR,  1.0f,    0.0f,    1.0f,           0.0f,     0.0f,      255.0f,    true  },
    { PID_CATALYST_TEMP,   "Catalyst Temperature B1S1",        "C",     2,     FORMULA_LINEAR,  256.0f,  1.0f,    0.1f,           -40.0f,   -40.0f,    6513.5f,   true  },
    { PID_MODULE_VOLTAGE,  "Control Module Voltage",           "V",     2,     FORMULA_LINEAR,  256.0f,  1.0f,    0.001f,         0.0f,     0.0f,      65.535f,   true  },
    { PID_REL_THROTTLE,    "Relative Throttle Position",       "%",     1,     FORMULA_LINEAR,  1.0f,    0.0f,    0.392157f,      0.0f,     0.0f,      100.0f,    true  },
    { PID_ACCEL_D,         "Accelerator Pedal Position D",     "%",     1,     FORMULA_LINEAR,  1.0f,    0.0f,    0.392157f,      0.0f,     0.0f,      100.0f,    true  },
    { PID_ACCEL_E,         "Accelerator Pedal Position E",     "%",     1,     FORMULA_LINEAR,  1.0f,    0.0f,    0.392157f,      0.0f,     0.0f,      100.0f,    true  },
    { PID_THROTTLE_ACT,    "Commanded Throttle Actuator",      "%",     1,     FORMULA_LINEAR,  1.0f,    0.0f,    0.392157f,      0.0f,     0.0f,      100.0f,    true  },
    { PID_TIME_MIL,        "Time Run with MIL On",             "min",   2,     FORMULA_LINEAR,  256.0f,  1.0f,    1.0f,           0.0f,     0.0f,      65535.0f,  true  },
    { PID_TIME_CLEARED,    "Time Since Codes Cleared",         "min",   2,     FORMULA_LINEAR,  256.0f,  1.0f,    1.0f,           0.0f,     0.0f,      65535.0f,  true  },
    { PID_STFT,            "Short Term Fuel Trim B1",          "%",     1,     FORMULA_LINEAR,  1.0f,    0.0f,    0.78125f,       -100.0f,  -100.0f,   99.2f,     false },
    { PID_LTFT,            "Long Term Fuel Trim B1",           "%",     1,     FORMULA_LINEAR,  1.0f,    0.0f,    0.78125f,       -100.0f,  -100.0f,   99.2f,     false },
    { PID_FUEL_PRESSURE,   "Fuel Pressure",                    "kPa",   1,     FORMULA_LINEAR,  1.0f,    0.0f,    3.0f,           0.0f,     0.0f,      765.0f,    false },
    { PID_O2_SENSOR,       "O2 Sensor 1 Lambda+Voltage",       "",      4,     FORMULA_COMPLEX, 0.0f,    0.0f,    0.0f,           0.0f,     0.0f,      0.0f,      false },
    { PID_ABS_LOAD,        "Absolute Load Value",              "%",     2,     FORMULA_LINEAR,  256.0f,  1.0f,    0.392157f,      0.0f,     0.0f,      25700.0f,  false },
    { PID_CMD_AFR,         "Commanded Air-Fuel Ratio",         "lambda",2,     FORMULA_LINEAR,  256.0f,  1.0f,    0.0000305176f,  0.0f,     0.0f,      2.0f,      false },
    { PID_AMBIENT_TEMP,    "Ambient Air Temperature",          "C",     1,     FORMULA_LINEAR,  1.0f,    0.0f,    1.0f,           -40.0f,   -40.0f,    215.0f,    false },
    { PID_THROTTLE_B,      "Absolute Throttle Position B",     "%",     1,     FORMULA_LINEAR,  1.0f,    0.0f,    0.392157f,      0.0f,     0.0f,      100.0f,    false },
    { PID_HYBRID_BATT,     "Hybrid Battery Remaining Life",    "%",     1,     FORMULA_LINEAR,  1.0f,    0.0f,    0.392157f,      0.0f,     0.0f,      100.0f,    false },
    { PID_OIL_TEMP,        "Engine Oil Temperature",           "C",     1,     FORMULA_LINEAR,  1.0f,    0.0f,    1.0f,           -40.0f,   -40.0f,    215.0f,    false },
    { PID_FUEL_RATE,       "Engine Fuel Rate",                 "L/h",   2,     FORMULA_LINEAR,  256.0f,  1.0f,    0.05f,          0.0f,     0.0f,      3276.75f,  false },
};

#define PID_MAP_SIZE (sizeof(PID_MAP) / sizeof(PID_MAP[0]))

// ---------- Mode 22 — UDS physical addressing ----------
// Manufacturer-specific "advanced" parameters are read with UDS service 0x22
// (ReadDataByIdentifier) using *physical* addressing: the request goes straight
// to one ECU and the response arrives on (request id + 8).  No diagnostic
// session (10 xx) or TesterPresent (3E 00) is required for these reads.
#define UDS_REQ_ENGINE   0x7E0u   // engine ECU  request → response 0x7E8
#define UDS_REQ_TCM      0x7E1u   // transmission ECU request → response 0x7E9
#define UDS_RESP_OFFSET  0x008u   // response id = request id + 8

// ---------- Mode 22 advanced-PID table ----------
// Real, reverse-engineered Mitsubishi advanced PIDs for the Pajero IV 3.2 DI-D
// (4M41).  Each entry says which ECU to ask, which DID to request, where the
// useful byte(s) live in the reassembled UDS payload, and how to scale them.
//
// `data_index` is the Torque data-byte index: D0 is the first byte *after* the
// 2-byte DID echo in the reassembled response (`62 DIDhi DIDlo D0 D1 …`).  Bytes
// at index >= 2 require ISO-TP multi-frame reassembly on the server side.
typedef enum {
    M22_U8,    // value = D[i]              * scale + offset
    M22_U16,   // value = (D[i]<<8 | D[i+1])* scale + offset   (big-endian, unsigned)
    M22_S16,   // value = SIGNED16(D[i],D[i+1]) * scale + offset
} Mode22FormulaType;

typedef struct {
    uint16_t          req_id;        // UDS_REQ_ENGINE / UDS_REQ_TCM
    uint16_t          did;           // UDS ReadDataByIdentifier DID
    uint8_t           data_index;    // index of D0 (first data byte after DID echo)
    Mode22FormulaType formula_type;
    float             scale;
    float             offset;
    uint8_t           slot_id;       // PID_M22_* DataAggregator key
    const char*       name;
    const char*       unit;
    bool              verified;      // confirmed against a raw capture on this vehicle
} Mode22AdvancedPid;

static const Mode22AdvancedPid MODE22_ADVANCED_PIDS[] = {
    // --- Engine ECU (0x7E0 → 0x7E8) ---
    { UDS_REQ_ENGINE, 0x20F2, 4, M22_U8,  1.0f, -40.0f, PID_M22_FUEL_TEMP,       "Fuel Temperature", "C",   false },
    { UDS_REQ_ENGINE, 0x2151, 1, M22_U8,  1.0f,   0.0f, PID_M22_FAN_DUTY,        "Cooling Fan Duty", "%",   false },
    // --- Transmission ECU (0x7E1 → 0x7E9) ---
    // DID 0x20AB carries both speeds: D1,D2 = input shaft, D3,D4 = output shaft.
    { UDS_REQ_TCM,    0x20AB, 1, M22_U16, 0.5f,   0.0f, PID_M22_AT_INPUT_SPEED,  "Trans Input Speed",  "rpm", false },
    { UDS_REQ_TCM,    0x20AB, 3, M22_U16, 0.5f,   0.0f, PID_M22_AT_OUTPUT_SPEED, "Trans Output Speed", "rpm", false },
};

#define MODE22_ADVANCED_PIDS_SIZE (sizeof(MODE22_ADVANCED_PIDS) / sizeof(MODE22_ADVANCED_PIDS[0]))
