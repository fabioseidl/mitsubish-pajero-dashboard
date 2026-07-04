# mitsubishi-pajero-dashboard

Distributed car dashboard built on ESP32. A server node reads vehicle data from the OBD-II port via CAN bus and broadcasts it wirelessly over ESP-NOW. Client nodes receive the data and render it on displays using LVGL.

Target vehicle: **Mitsubishi Pajero Dakar 3.2 Diesel**.

---

## Hardware

| Node | Components |
|---|---|
| Server | ESP32 + TWAI + CAN transceiver (OBD-II, 500 kbps) |
| Simple HUD client | ESP32 + 2.4" SPI display (LVGL + LovyanGFX) |
| Main display | Waveshare ESP32-S3 + 7" 1024×600 RGB parallel LCD + GT911 touch |

---

## Projects

Each sub-project under `projects/` is a standalone PlatformIO environment.

```
projects/server/            Reads CAN bus, broadcasts Payload via ESP-NOW (production server)
projects/server_emulator/   Generates synthetic driving data — client dev without the car
projects/server-prototype/  Earlier server prototype
projects/client_simple_hud/ LVGL + LovyanGFX HUD on a 2.4" SPI display
projects/main_display/      LVGL dashboard on Waveshare ESP32-S3 7" RGB LCD + GT911 touch
projects/gps_debug/         GPS module bring-up / debugging on the ESP32-S3 board
projects/sniffer/           CAN sniffer + DBC files + monitor.py for PID reverse engineering
```

Shared code lives in `lib/core/` (payload struct, PID map, ESP-NOW broadcaster/receiver, brightness and connection helpers).

---

## Architecture

One ESP32 reads OBD-II data over the CAN bus and broadcasts a packed `Payload` struct at 10 Hz via ESP-NOW to a broadcast address. Multiple clients receive and render it. Communication is unidirectional — there are no ACKs or reverse messages.

**Server data flow:**
```
CAN bus → CANDriver → PIDDictionary + PIDTranslator → DataAggregator (mutex)
        → DerivedCalculator → SessionAccumulator → PayloadBuilder → ESPNowBroadcaster
```

**Client data flow:**
```
ESP-NOW ISR → ESPNowReceiver → ServerConnectionMonitor + IScreenController → LVGL widgets
```

Session accumulation (distance, average consumption) is computed **server-side** and carried in the Payload; clients are pure renderers.

The `Payload` struct is a fixed-size packed struct; a `static_assert` in `lib/core/include/payload.h` enforces its exact byte size at compile time. Use fixed-width integer types only in Payload fields (host arm64 vs ESP32 sizes differ).

---

## Setup

**1. Clone and create the security config**

```bash
git clone git@github.com:fabioseidl/mitsubish-pajero-dashboard.git
cd mitsubish-pajero-dashboard
cp lib/core/include/security_config.h.example \
   lib/core/include/security_config.h
```

Edit `security_config.h` and set a real 16-byte PMK. This file is gitignored — never commit it.

**2. Run host tests**

```bash
cd test && pio test -e native_tests
```

All tests must pass before flashing. Tests use Unity, compile with `-DUNIT_TEST -std=c++17`, and mock the hardware drivers (CAN, display, ESP-NOW).

**3. Build and flash**

Run from each sub-project directory:

```bash
pio run                    # build only
pio run --target upload    # build and flash to hardware
```

For example:

```bash
cd projects/server        && pio run --target upload
cd projects/main_display  && pio run --target upload
```

Use `server_emulator` instead of `server` when developing without the vehicle.

---

## Development

This project follows spec-driven development: write specifications before implementing features, and validate with comprehensive host tests. Full documentation is in `docs/`:

- `00_overview.md` — system overview, hardware, design decisions
- `01_architecture.md` — component diagram, FreeRTOS tasks, derived value formulas
- `02_data_model.md` — PID map and formula encoding
- `03_server_spec.md` / `04_client_lib_spec.md` — class specifications
- `05_folder_structure.md` — repository layout
- `06_test_spec.md` — test methodology and full test suites

The OBD-II PID map (`lib/core/include/pid_map.h`) is hand-maintained — add new PIDs manually with explicit formula parameters; do not auto-generate.

See `CLAUDE.md` for Claude Code session rules.
