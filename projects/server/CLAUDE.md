# CLAUDE.md — projects/server

Read the root `CLAUDE.md` and `docs/03_server_spec.md` before working on this project.

---

## Purpose

Reads OBD-II data from the vehicle CAN bus via MCP2515 + TJA1050, translates raw frames into engineering values, computes derived metrics, accumulates session data, and broadcasts a `Payload` struct via ESP-NOW at 10 Hz.

---

## Hardware

| Component | Detail |
|---|---|
| MCU | ESP32 |
| CAN controller | MCP2515 (SPI) |
| CAN transceiver | TJA1050 |
| CAN speed | 500 kbps |
| Target vehicle | Mitsubishi Pajero Dakar (diesel) |

Pin assignments must be defined as named constants in `include/pin_config.h`. Never use raw GPIO numbers inline in code.

---

## Classes in This Project

| Class | File | Responsibility |
|---|---|---|
| `ICANDriver` | `include/ican_driver.h` | Interface for CAN hardware abstraction |
| `CANDriver` | `include/can_driver.h` | MCP2515 SPI implementation |
| `PIDDictionary` | `include/pid_dictionary.h` | Lookup from (can_id, pid_code) to PidDefinition |
| `PIDTranslator` | `include/pid_translator.h` | Raw bytes → engineering float; bitmask extraction |
| `DataAggregator` | `include/data_aggregator.h` | Thread-safe latest-value store per PID |
| `DerivedCalculator` | `include/derived_calculator.h` | Instantaneous consumption from speed + fuel rate |
| `SessionAccumulator` | `include/session_accumulator.h` | Integrated distance and fuel since boot |
| `PayloadBuilder` | `include/payload_builder.h` | Assembles `Payload` from aggregator + session |

Classes from `lib/core` also used here: `ESPNowBroadcaster`, `Payload`, `pid_map.h`.

---

## FreeRTOS Tasks

| Task | Core | Priority | Touches |
|---|---|---|---|
| `can_rx_task` | 1 | 5 | `CANDriver`, `PIDDictionary`, `PIDTranslator`, `DataAggregator` |
| `broadcast_task` | 0 | 3 | `DerivedCalculator`, `SessionAccumulator`, `PayloadBuilder`, `ESPNowBroadcaster` |

`DataAggregator` is shared between tasks — it is mutex-protected. No other shared state between tasks is permitted.

---

## OBD-II Protocol Notes

- OBD-II requests are sent on CAN ID `0x7DF`
- ECU responses arrive on CAN ID `0x7E8`
- PID code is at byte index 2 of the response frame data
- PID `0x01` returns a 4-byte bitmask — use `PIDTranslator::extractMilStatus()` and `extractDtcCount()`, do not pass to `translate()`

---

## PID Reference

All supported PIDs are defined in `lib/core/include/pid_map.h`. Do not hardcode PID values — always use the `PID_*` defines. PIDs marked `verified = false` may return no data from this vehicle.

Required PIDs for a valid broadcast (checked by `DataAggregator::allRequiredPidsReceived()`):
- `PID_RPM` (0x0C)
- `PID_SPEED` (0x0D)
- `PID_FUEL_RATE` (0x5E) — unverified, consumption will be 0.0 if unsupported

---

## Derived Value Formulas

```
// Instantaneous consumption
if speed > 0 AND fuel_rate > 0:
    consumption_km_per_l = speed_km_h / fuel_rate_l_per_h
else:
    consumption_km_per_l = 0.0

// Session distance (per tick)
delta_distance = speed_km_h * (delta_ms / 3_600_000.0f)

// Session avg consumption
avg = total_distance_km / total_fuel_l  (0.0 if total_fuel == 0)
```

---

## Tests for This Project

All tests are in `test/host/server/`. Run from repo root:

```bash
cd test && pio test -e native_tests
```

Test files:
- `test_pid_translator.cpp`
- `test_pid_dictionary.cpp`
- `test_data_aggregator.cpp`
- `test_derived_calculator.cpp`
- `test_session_accumulator.cpp`
- `test_payload_builder.cpp`

`CANDriver` is excluded from host tests — test via `MockCANDriver` (see `test/host/mocks/mock_can_driver.h`).

---

## Build

```bash
cd projects/server
pio run
pio run --target upload
```
