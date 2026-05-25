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
// Mode 22 real PIDs (0xF1xx / 0xF3xx) exceed the DataAggregator's uint8-indexed
// store.  Each real PID is remapped to a slot ID in the 0xA0–0xBF range, which
// is unused by all Mode 01 PIDs.  The server's Mode 22 response handler performs
// the real_pid → slot_id lookup via MODE22_PID_MAP below.
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

// ---------- Mode 22 PID mapping table ----------
// Maps the real 16-bit UDS PID to its DataAggregator slot ID and metadata.
// All entries are unverified — the ECU returned negative responses during
// initial scanning; formulas and data_bytes are unknown until confirmed.
typedef struct {
    uint16_t    real_pid;   // UDS ReadDataByIdentifier PID sent on the bus
    uint8_t     slot_id;    // PID_M22_* define used as DataAggregator key
    const char* name;
    const char* unit;
} Mode22PidDefinition;

static const Mode22PidDefinition MODE22_PID_MAP[] = {
    // AT ECU — responses on CAN ID 0x7E9
    { 0xF100, PID_M22_AT_GEAR_POS,     "AT Current Gear Position",       ""    },
    { 0xF101, PID_M22_AT_GEAR_RATIO,   "AT Gear Ratio",                  ""    },
    { 0xF102, PID_M22_AT_INPUT_SPEED,  "AT Input Shaft Speed",           "rpm" },
    { 0xF103, PID_M22_AT_OUTPUT_SPEED, "AT Output Shaft Speed",          "rpm" },
    { 0xF104, PID_M22_AT_TC_SLIP,      "AT Torque Converter Slip",       "rpm" },
    { 0xF105, PID_M22_AT_ATF_TEMP,     "AT ATF Temperature",             "C"   },
    { 0xF106, PID_M22_AT_SHIFT_SOL,    "AT Shift Solenoid Status",       ""    },
    { 0xF107, PID_M22_AT_LOCKUP,       "AT Lock-up Status",              ""    },
    { 0xF108, PID_M22_AT_PRNDL,        "AT Gear Selector (PRNDL)",       ""    },
    { 0xF109, PID_M22_AT_TARGET_GEAR,  "AT Target Gear",                 ""    },
    { 0xF10A, PID_M22_AT_OIL_PRES,     "AT Transmission Oil Pressure",   ""    },
    // Engine ECU — responses on CAN ID 0x7E8
    { 0xF300, PID_M22_BOOST_PRES,      "Boost Pressure (turbo)",         ""    },
    { 0xF301, PID_M22_EGR_VALVE_POS,   "EGR Valve Position",             ""    },
    { 0xF302, PID_M22_DPF_SOOT,        "DPF Soot Load",                  ""    },
    { 0xF303, PID_M22_DPF_REGEN,       "DPF Regeneration Status",        ""    },
    { 0xF304, PID_M22_RAIL_PRES_ACT,   "Fuel Rail Pressure — Actual",    ""    },
    { 0xF305, PID_M22_RAIL_PRES_DES,   "Fuel Rail Pressure — Desired",   ""    },
    { 0xF306, PID_M22_INJ_COR_CYL1,    "Injector Correction Cyl 1",      ""    },
    { 0xF307, PID_M22_INJ_COR_CYL2,    "Injector Correction Cyl 2",      ""    },
    { 0xF308, PID_M22_INJ_COR_CYL3,    "Injector Correction Cyl 3",      ""    },
    { 0xF309, PID_M22_INJ_COR_CYL4,    "Injector Correction Cyl 4",      ""    },
};

#define MODE22_PID_MAP_SIZE (sizeof(MODE22_PID_MAP) / sizeof(MODE22_PID_MAP[0]))
