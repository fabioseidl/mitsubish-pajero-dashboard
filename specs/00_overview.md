# Car Dashboard ESP32 — Project Overview

## Summary

A distributed car dashboard system built on ESP32 microcontrollers. One ESP32 acts as a CAN bus server, reading vehicle data via MCP2515 + TJA1050 from the OBD-II port, translating it, computing derived metrics, and broadcasting it wirelessly. One or more ESP32 clients receive the data and render it on displays using LVGL.

A server emulator project replicates the server broadcast behavior without CAN hardware, enabling client development independently of the vehicle.

---

## Documentation Index

| File | Contents |
|---|---|
| `01_architecture.md` | System architecture, communication flow, component responsibilities |
| `02_data_model.md` | PID map, shared payload struct, formula encoding |
| `03_server_spec.md` | Server and emulator classes, full method documentation |
| `04_client_lib_spec.md` | Shared client library classes, full method documentation |
| `05_folder_structure.md` | Monorepo layout, PlatformIO configs per project |
| `06_test_spec.md` | TDD test plan, Unity test cases per module, host mock strategy |

---

## Hardware

### Server Node
- MCU: ESP32 (any variant with SPI)
- CAN controller: MCP2515 (SPI)
- CAN transceiver: TJA1050
- Connection: OBD-II port (CAN High / CAN Low)
- CAN bus speed: 500 kbps
- Target vehicle: Mitsubishi Pajero Dakar (diesel)

### Client Node — v1 (CYD)
- MCU: ESP32 (Cheap Yellow Display board)
- Display: ILI9341, 2.4 inch, 320x240px, SPI
- Touch: XPT2046 resistive touch controller
- UI framework: LVGL v8.x

---

## Development Environment

- Framework: ESP-IDF (via PlatformIO)
- IDE: VSCode + PlatformIO extension
- Build system: PlatformIO (monorepo, separate `platformio.ini` per sub-project)
- Testing methodology: Test-Driven Development (TDD)
- Test framework: Unity (run on host)
- Host machine: macOS ARM64 (Apple M2)
- Language: C++

---

## Design Decisions Log

| # | Decision | Rationale |
|---|---|---|
| 1 | ESP-NOW for wireless | Low latency, no router dependency, native to ESP32 |
| 2 | Unidirectional broadcast | Simplifies server; clients filter what they need |
| 3 | PMK-only security | Adequate for v1; LMK per-device deferred to future |
| 4 | Hand-maintained `pid_map.h` | Removes codegen dependency; simpler build pipeline |
| 5 | Server computes derived values | Clients are pure renderers; keeps client firmware minimal |
| 6 | PID 0x5E for fuel rate | Vehicle is diesel; MAF-based stoichiometric formula unreliable for diesel |
| 7 | LVGL v8.x for client UI | Mature embedded graphics library; stable ESP-IDF + PlatformIO support |
| 8 | Unity on host | Faster iteration; hardware-dependent code abstracted behind interfaces |
| 9 | Separate `platformio.ini` per sub-project | Different targets, deps, and flash configs; a single root file becomes brittle |
| 10 | Server emulator project | Enables client development without vehicle or CAN hardware |
| 11 | `SessionAccumulator` on server | Distance and avg consumption require time-integrated state; belongs server-side |
| 12 | `0x01` bitmask split server-side | Raw bitmask irrelevant to display; extract `mil_on` + `dtc_count` only |
| 13 | `0x1C` excluded entirely | OBD standard enum has no display value for this use case |

---

## PID Support Status

| Status | Meaning |
|---|---|
| Confirmed | Tested and responded correctly on target vehicle |
| Unverified | Standard PID, not yet tested on target vehicle |
| Rejected | ECU returned negative response |
| Excluded | Deliberately not included |

---

## Known Risks

| Risk | Impact | Mitigation |
|---|---|---|
| PID 0x5E not supported by ECU | `consumption_km_per_l` and `avg_consumption_km_per_l` always 0.0 | Verify on vehicle before release |
| Unverified PIDs may be unsupported | Fields from those PIDs will be 0.0 | Each PID marked in `pid_map.h`; test on vehicle during development |
| Mode 22 proprietary PIDs all rejected | Gear, boost, DPF, rail pressure unavailable | Documented as ECU limitation; no workaround |
| `sizeof(Payload)` differs between ARM64 host and ESP32 | Silent data corruption on wire | `static_assert` in firmware + Unity size test on host |

---

## v1 Scope Boundaries

**In scope:**
- Reading all confirmed OBD-II PIDs from target vehicle
- Broadcasting `Payload` struct via ESP-NOW (PMK-secured) at 10 Hz
- Session-scoped distance and average fuel consumption (server-side)
- Rendering RPM, speed, instantaneous and average consumption, distance, MIL status on CYD 2.4" via LVGL
- Touch-based brightness cycling (4 levels: 25%, 50%, 75%, 100%)
- Server emulator for client development without hardware

**Out of scope for v1:**
- Proprietary/manufacturer-specific PIDs (all rejected by ECU)
- Multiple simultaneous client types
- LMK per-client encryption
- User-configurable layouts
- DTC reading and display beyond MIL on/off + count
- Data logging or persistent storage
- Session persistence across reboots
