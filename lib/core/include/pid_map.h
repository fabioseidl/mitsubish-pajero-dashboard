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

#define PID_MONITOR_STATUS  0x01u
#define PID_ENGINE_LOAD     0x04u
#define PID_COOLANT_TEMP    0x05u
#define PID_MAP_PRESSURE    0x0Bu
#define PID_RPM             0x0Cu
#define PID_SPEED           0x0Du
#define PID_INTAKE_AIR_TEMP 0x0Fu
#define PID_MAF             0x10u
#define PID_THROTTLE        0x11u
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
