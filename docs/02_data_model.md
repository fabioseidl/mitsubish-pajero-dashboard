# Data Model

## PID Map — `lib/core/include/pid_map.h`

The PID map is a hand-maintained C header committed to version control. It is the single source of truth for all supported PIDs, their translation formulas, and their verification status.

### Formula Encoding

All linear OBD-II formulas follow the pattern:

```
value = (byte_A * a_mult + byte_B * b_mult) * scale + offset
```

Where `byte_A` is the first data byte and `byte_B` is the second. For single-byte PIDs, `b_mult = 0.0f`.

Non-linear PIDs use a `formula_type` enum to trigger special handling in `PIDTranslator`.

### `pid_map.h` Full Content

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>

/**
 * Formula types for PID translation.
 *
 * FORMULA_LINEAR   : value = (A * a_mult + B * b_mult) * scale + offset
 * FORMULA_BITMASK  : raw bytes require bit extraction (see PIDTranslator)
 * FORMULA_COMPLEX  : multi-value or non-linear (see PIDTranslator)
 */
typedef enum {
    FORMULA_LINEAR,
    FORMULA_BITMASK,
    FORMULA_COMPLEX,
} FormulaType;

/**
 * Full definition of a single OBD-II PID.
 */
typedef struct {
    uint16_t    pid;          // OBD-II PID code (e.g. 0x0C)
    const char* name;         // Human-readable name
    const char* unit;         // Engineering unit string
    uint8_t     data_bytes;   // Expected data bytes in response (1–4)
    FormulaType formula_type;
    float       a_mult;       // Multiplier for byte A
    float       b_mult;       // Multiplier for byte B
    float       scale;        // Scale applied after byte combination
    float       offset;       // Offset applied after scaling
    float       min_value;    // Minimum valid output value
    float       max_value;    // Maximum valid output value
    bool        verified;     // true = confirmed on target vehicle
} PidDefinition;

// ─────────────────────────────────────────────────────────────────────────────
// CONFIRMED PIDs — tested and responded correctly on Pajero Dakar (diesel)
// ─────────────────────────────────────────────────────────────────────────────

// PID 0x01 — Monitor Status Since DTCs Cleared
// Special: 4-byte bitmask. Translated into mil_on + dtc_count by PIDTranslator.
// Not represented as a single float; formula fields unused.
#define PID_MONITOR_STATUS  0x01u

// PID 0x04 — Calculated Engine Load
// Formula: A / 2.55  →  % (0–100)
#define PID_ENGINE_LOAD     0x04u

// PID 0x05 — Engine Coolant Temperature
// Formula: A - 40  →  °C (-40 to 215)
#define PID_COOLANT_TEMP    0x05u

// PID 0x0B — Intake Manifold Absolute Pressure
// Formula: A  →  kPa (0–255)
#define PID_MAP_PRESSURE    0x0Bu

// PID 0x0C — Engine RPM
// Formula: (256*A + B) / 4  →  rpm (0–16383.75)
#define PID_RPM             0x0Cu

// PID 0x0D — Vehicle Speed
// Formula: A  →  km/h (0–255)
#define PID_SPEED           0x0Du

// PID 0x0F — Intake Air Temperature
// Formula: A - 40  →  °C (-40 to 215)
#define PID_INTAKE_AIR_TEMP 0x0Fu

// PID 0x10 — MAF Air Flow Rate
// Formula: (256*A + B) / 100  →  g/s (0–655.35)
// Note: kept as a readable sensor. No longer used for consumption calculation.
#define PID_MAF             0x10u

// PID 0x11 — Throttle Position
// Formula: A / 2.55  →  % (0–100)
#define PID_THROTTLE        0x11u

// PID 0x1F — Run Time Since Engine Start
// Formula: 256*A + B  →  seconds (0–65535)
#define PID_RUNTIME         0x1Fu

// PID 0x21 — Distance Traveled with MIL On
// Formula: 256*A + B  →  km (0–65535)
#define PID_DIST_MIL        0x21u

// PID 0x23 — Fuel Rail Gauge Pressure (diesel)
// Formula: (256*A + B) * 10  →  kPa (0–655350)
#define PID_FUEL_RAIL_PRES  0x23u

// PID 0x2C — Commanded EGR
// Formula: A / 2.55  →  % (0–100)
#define PID_EGR_CMD         0x2Cu

// PID 0x2D — EGR Error
// Formula: A / 1.28 - 100  →  % (-100 to +99.2)
#define PID_EGR_ERROR       0x2Du

// PID 0x30 — Warm-ups Since Codes Cleared
// Formula: A  →  count (0–255)
#define PID_WARMUPS         0x30u

// PID 0x31 — Distance Traveled Since Codes Cleared
// Formula: 256*A + B  →  km (0–65535)
#define PID_DIST_CLEARED    0x31u

// PID 0x33 — Absolute Barometric Pressure
// Formula: A  →  kPa (0–255)
#define PID_BARO_PRESSURE   0x33u

// PID 0x3C — Catalyst Temperature, Bank 1 Sensor 1
// Formula: (256*A + B) / 10 - 40  →  °C (-40 to 6513.5)
#define PID_CATALYST_TEMP   0x3Cu

// PID 0x42 — Control Module Voltage
// Formula: (256*A + B) / 1000  →  V (0–65.535)
#define PID_MODULE_VOLTAGE  0x42u

// PID 0x45 — Relative Throttle Position
// Formula: A / 2.55  →  % (0–100)
#define PID_REL_THROTTLE    0x45u

// PID 0x49 — Accelerator Pedal Position D
// Formula: A / 2.55  →  % (0–100)
#define PID_ACCEL_D         0x49u

// PID 0x4A — Accelerator Pedal Position E
// Formula: A / 2.55  →  % (0–100)
#define PID_ACCEL_E         0x4Au

// PID 0x4C — Commanded Throttle Actuator
// Formula: A / 2.55  →  % (0–100)
#define PID_THROTTLE_ACT    0x4Cu

// PID 0x4D — Time Run with MIL On
// Formula: 256*A + B  →  minutes (0–65535)
#define PID_TIME_MIL        0x4Du

// PID 0x4E — Time Since Trouble Codes Cleared
// Formula: 256*A + B  →  minutes (0–65535)
#define PID_TIME_CLEARED    0x4Eu

// ─────────────────────────────────────────────────────────────────────────────
// UNVERIFIED PIDs — standard OBD-II, not yet tested on target vehicle.
// Fields populated from these PIDs will read 0.0 if ECU returns negative response.
// ─────────────────────────────────────────────────────────────────────────────

// PID 0x06 — Short Term Fuel Trim, Bank 1
// Formula: A / 1.28 - 100  →  % (-100 to +99.2)
#define PID_STFT            0x06u

// PID 0x07 — Long Term Fuel Trim, Bank 1
// Formula: A / 1.28 - 100  →  % (-100 to +99.2)
#define PID_LTFT            0x07u

// PID 0x0A — Fuel Pressure
// Formula: A * 3  →  kPa (0–765)
#define PID_FUEL_PRESSURE   0x0Au

// PID 0x24 — O2 Sensor 1 — Lambda + Voltage
// Special: 4-byte complex encoding. Handled as FORMULA_COMPLEX.
#define PID_O2_SENSOR       0x24u

// PID 0x43 — Absolute Load Value
// Formula: (256*A + B) / 2.55  →  % (0–25700)
#define PID_ABS_LOAD        0x43u

// PID 0x44 — Commanded Air-Fuel Equivalence Ratio (lambda)
// Formula: (256*A + B) * (2.0 / 65536.0)  →  ratio (0–2)
#define PID_CMD_AFR         0x44u

// PID 0x46 — Ambient Air Temperature
// Formula: A - 40  →  °C (-40 to 215)
#define PID_AMBIENT_TEMP    0x46u

// PID 0x47 — Absolute Throttle Position B
// Formula: A / 2.55  →  % (0–100)
#define PID_THROTTLE_B      0x47u

// PID 0x5B — Hybrid Battery Pack Remaining Life
// Formula: A / 2.55  →  % (0–100)
#define PID_HYBRID_BATT     0x5Bu

// PID 0x5C — Engine Oil Temperature
// Formula: A - 40  →  °C (-40 to 215)
#define PID_OIL_TEMP        0x5Cu

// PID 0x5E — Engine Fuel Rate  *** REQUIRED FOR CONSUMPTION CALCULATION ***
// Formula: (256*A + B) / 20  →  L/h (0–3276.75)
#define PID_FUEL_RATE       0x5Eu

// ─────────────────────────────────────────────────────────────────────────────
// PID Definition Table
// ─────────────────────────────────────────────────────────────────────────────

static const PidDefinition PID_MAP[] = {
    // pid,                 name,                               unit,    bytes, formula_type,    a_mult,  b_mult,  scale,          offset,   min,       max,       verified
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
    // --- UNVERIFIED ---
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
```

---

## Rejected PIDs (Mode 22)

The following PIDs were tested on the target vehicle using UDS Mode 0x22 and returned negative responses. They are not implemented.

| Key | Name | ECU Response | NRC Meaning |
|---|---|---|---|
| F100–F10A | AT transmission data | `7F 22 80` | requestOutOfRange |
| F300–F309 | Engine/DPF/boost data | `7F 22 11` | serviceNotSupported |

---

## Payload Struct — `lib/core/include/payload.h`

```cpp
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define PAYLOAD_VERSION 1

/**
 * Fixed-size packed struct transmitted via ESP-NOW.
 * Populated by the server, consumed by all clients.
 * Maximum ESP-NOW payload: 250 bytes. Current size: 27 bytes.
 */
typedef struct __attribute__((packed)) {
    uint8_t  version;                  // Must equal PAYLOAD_VERSION
    uint32_t timestamp_ms;             // Server uptime in ms at broadcast time

    // ── Primary display metrics ──────────────────────────────────────────────
    uint16_t rpm;                      // Engine RPM (PID 0x0C), 0–16383
    uint8_t  speed_kmh;                // Vehicle speed (PID 0x0D), 0–255 km/h
    float    fuel_rate_l_per_h;        // Fuel rate (PID 0x5E), L/h
    float    consumption_km_per_l;     // Instantaneous fuel consumption, km/L
    float    avg_consumption_km_per_l; // Session average fuel consumption, km/L
    float    distance_km;              // Session distance since server start, km

    // ── Diagnostics ──────────────────────────────────────────────────────────
    bool     mil_on;                   // MIL (check engine light) active
    uint8_t  dtc_count;               // Number of stored DTCs

    // ── Status flags ─────────────────────────────────────────────────────────
    uint8_t  flags;                    // Bit field, see defines below
} Payload;

// flags bit positions
#define PAYLOAD_FLAG_DATA_VALID     (1 << 0) // All required PIDs received
#define PAYLOAD_FLAG_ENGINE_RUNNING (1 << 1) // RPM > 400

static_assert(sizeof(Payload) == 27,
    "Payload size mismatch — check struct fields and packing");
```

### Size Breakdown

| Field | Type | Bytes |
|---|---|---|
| version | uint8_t | 1 |
| timestamp_ms | uint32_t | 4 |
| rpm | uint16_t | 2 |
| speed_kmh | uint8_t | 1 |
| fuel_rate_l_per_h | float | 4 |
| consumption_km_per_l | float | 4 |
| avg_consumption_km_per_l | float | 4 |
| distance_km | float | 4 |
| mil_on | bool | 1 |
| dtc_count | uint8_t | 1 |
| flags | uint8_t | 1 |
| **Total** | | **27 bytes** |

Note: `static_assert` value must be verified at compile time on the actual toolchain. The table above assumes `bool` is 1 byte and `__attribute__((packed))` eliminates all padding.
