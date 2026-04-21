# CLAUDE.md — projects/server_emulator

Read the root `CLAUDE.md` and `docs/03_server_spec.md` before working on this project.

---

## Purpose

Produces the exact same ESP-NOW broadcast as `projects/server` but without any CAN hardware or vehicle connection. Generates synthetic sensor data following configurable driving profiles. Used to develop and test clients independently of the vehicle.

---

## Key Constraint

This project must produce a `Payload` struct that is **byte-for-byte identical in format** to what the real server produces. The `Payload` definition lives in `lib/core/include/payload.h` and must never be modified here. If the real server's `Payload` changes, the emulator automatically benefits since it shares the same struct.

---

## Hardware

| Component | Detail |
|---|---|
| MCU | ESP32 (any variant) |
| CAN | None — not connected |
| Display | None |

---

## Classes in This Project

| Class | File | Responsibility |
|---|---|---|
| `SimulationDataGenerator` | `include/simulation_data_generator.h` | Generates synthetic values for all Payload fields per driving profile |

Classes from `lib/core` also used here: `ESPNowBroadcaster`, `Payload`, `SessionAccumulator`.

---

## Driving Profiles

```cpp
enum class DrivingProfile {
    IDLE,     // RPM ~800, speed 0, low fuel rate
    CITY,     // RPM 1000–3000, speed 0–60 km/h, sinusoidal variation
    HIGHWAY,  // RPM 1800–2500, speed 80–120 km/h, low variation
};
```

Profiles use sinusoidal functions of `elapsed_ms_` to produce smooth, realistic variation. The profile can be changed at runtime via `setProfile()` without resetting the session accumulator.

---

## Simulation Rules

- RPM and speed must never go negative
- Fuel rate must be proportional to RPM and load — do not generate independent random values
- `mil_on` is always `false` in simulation
- `dtc_count` is always `0` in simulation
- `PAYLOAD_FLAG_DATA_VALID` must always be set in simulation — the client should always receive valid data
- `PAYLOAD_FLAG_ENGINE_RUNNING` must reflect whether simulated RPM > 400

---

## Broadcast Behavior

- Same PMK as real server (from `security_config.h`)
- Same broadcast address (`FF:FF:FF:FF:FF:FF`)
- Same rate: 10 Hz (100 ms tick)
- Uses `ESPNowBroadcaster` from `lib/core` — do not reimplement ESP-NOW here

---

## Tests for This Project

`SimulationDataGenerator` is tested in `test/host/` with 80% coverage target. Key behaviors to test:

- `getPayload()` after `tick()` returns non-negative RPM and speed for all profiles
- `PAYLOAD_FLAG_DATA_VALID` is always set
- Session accumulates distance and fuel across ticks
- `setProfile()` does not reset session
- `tick(0)` does not crash or accumulate

---

## Build

```bash
cd projects/server_emulator
pio run
pio run --target upload
```

Flash this instead of the real server when developing or testing client layouts on the bench.
