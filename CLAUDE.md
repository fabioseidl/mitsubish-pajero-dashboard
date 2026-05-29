# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Commands

**Build & Flash (run from each sub-project directory):**
```bash
pio run                    # build only
pio run --target upload    # build and flash to hardware
```

**Run all host tests (from repo root):**
```bash
cd test && pio test -e native_tests
```

**First-time setup:**
```bash
cp lib/core/include/security_config.h.example lib/core/include/security_config.h
# Edit security_config.h and set a real 16-byte PMK
```

## Architecture

A distributed car dashboard for a Mitsubishi Pajero Dakar. One ESP32 reads OBD-II data over CAN bus and broadcasts it wirelessly via ESP-NOW; multiple ESP32 clients receive and render the data on displays using LVGL.

**Sub-projects** (`projects/`):
- `server/` — reads CAN frames via TWAI at 500 kbps, translates PIDs, accumulates session data, and broadcasts a `Payload` struct at 10 Hz via ESP-NOW
- `server_emulator/` — same ESP-NOW broadcast as server but generates synthetic sinusoidal driving profiles instead of reading from CAN; enables client development without the vehicle
- `client_simple_hud/` — LVGL + LovyanGFX on ESP32, renders the dashboard on a small 2.4" SPI display
- `main_display/` — LVGL on Waveshare ESP32-S3 with a 7" 1024×600 RGB parallel LCD + GT911 touch

**Shared library** (`lib/core/`):
- `payload.h` — central 221-byte packed struct; a `static_assert` enforces the exact size
- `pid_map.h` — hand-maintained OBD-II PID definitions with formula parameters; never auto-generate
- `ESPNowBroadcaster` / `ESPNowReceiver` — wireless abstraction (broadcast to `FF:FF:FF:FF:FF:FF`, PMK security)
- `BrightnessController`, `ServerConnectionMonitor`, `IDisplay` — shared logic across all clients

**Host tests** (`test/`):
- Unity framework running on macOS native (ARM64); all tests compile with `-DUNIT_TEST` and `-std=c++17`
- Mocks in `test/host/mocks/` replace hardware drivers (CAN, display, ESP-NOW)
- Tests cover all server-side classes and shared lib classes; run these before flashing

**Server data flow:**
```
CAN bus → CANDriver → PIDDictionary + PIDTranslator → DataAggregator (mutex)
        → DerivedCalculator → SessionAccumulator → PayloadBuilder → ESPNowBroadcaster
```

**Client data flow:**
```
ESP-NOW ISR → ESPNowReceiver → ServerConnectionMonitor + IScreenController → LVGL widgets
```

**FreeRTOS tasks on server:** `can_rx_task` (core 1, priority 5) and `broadcast_task` (core 0, priority 3). `DataAggregator` is the only shared state between tasks, protected by a mutex.

## Key constraints

- **Payload size**: must remain exactly 221 bytes; `static_assert` in `payload.h` enforces this at compile time
- **Fixed-width types only** in `Payload` fields — never `int`, `long`, or `size_t` (arm64 host vs ESP32 sizes differ)
- **PID map is hand-maintained**: add new PIDs manually with explicit formula parameters; do not generate code
- **`security_config.h` is gitignored**: never commit it; always create from `.example`
- **Session accumulation is server-side**: clients are pure renderers; distance and average consumption are computed on the server and carried in the Payload
- **ESP-NOW is unidirectional**: server broadcasts, clients receive — no ACKs or reverse messages

## Documentation

Detailed specs live in `docs/`:
- `00_overview.md` — system overview, hardware, design decisions log
- `01_architecture.md` — component diagram, FreeRTOS tasks, derived value formulas
- `02_data_model.md` — PID map, formula encoding
- `03_server_spec.md` / `04_client_lib_spec.md` — full class specifications
- `06_test_spec.md` — spec-driven development methodology and full test suites

Sub-project-level `CLAUDE.md` files exist in `projects/server/` and `projects/server_emulator/` with additional hardware-specific details.
