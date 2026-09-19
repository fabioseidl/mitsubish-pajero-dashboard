# mitsubishi-pajero-dashboard

Distributed car dashboard built on ESP32. A server node reads vehicle data from
the OBD-II port via CAN bus and broadcasts it wirelessly over ESP-NOW. Client
nodes receive the data and render it on displays using LVGL.

Target vehicle: **Mitsubishi Pajero Dakar 3.2 Diesel** (Pajero IV, 4M41).

---

## Hardware

| Node | Components |
|---|---|
| Server | ESP32-S3 + MCP2515 CAN controller (OBD-II, 500 kbps) |
| Main display | Waveshare ESP32-S3-Touch-LCD-7B — 7" 1024×600 RGB LCD, GPS, IMU, environment sensors |
| Main HUD | Guition JC3248W535 (ESP32-S3) — 3.5" 320×480 QSPI, windshield-reflected |
| Glass display | Waveshare ESP32-C6-LCD-1.47 — 1.47" 172×320 SPI |
| Simple HUD | CYD (ESP32) — 2.4" 320×240 SPI, LDR auto-brightness |

---

## Projects

Each sub-project under `projects/` is a standalone PlatformIO environment.

```
projects/server/            Reads CAN bus, broadcasts Payload via ESP-NOW (production server)
projects/server_emulator/   Generates synthetic driving data — client dev without the car
projects/main_display/      7" LVGL dashboard (SquareLine UI) + GPS/IMU/environment sensors
projects/main_hud/          3.5" QSPI speed-only HUD with windshield mirroring
projects/glass_display/     1.47" ESP32-C6 speed-only client
projects/client_simple_hud/ 2.4" CYD HUD (SquareLine UI, LDR auto-brightness)
projects/sniffer/           CAN sniffer + DBC files + monitor.py for PID reverse engineering
projects/server-prototype/  Earlier server prototype (legacy, reference only)
```

Shared code lives in `lib/core/` — the `Payload` struct, the PID map, the ESP-NOW
broadcaster/receiver, and the display/brightness/connection helpers.

---

## Architecture

One ESP32 reads OBD-II data over the CAN bus and broadcasts a packed `Payload`
struct at 10 Hz to the ESP-NOW broadcast address. Multiple clients receive and
render it. Communication is unidirectional — there are no ACKs or reverse messages.

**Server data flow**
```
CAN bus → CANDriver → PIDDictionary + PIDTranslator → DataAggregator (mutex)
        → DerivedCalculator → SessionAccumulator → PayloadBuilder → ESPNowBroadcaster
```

**Client data flow**
```
ESP-NOW ISR → ESPNowReceiver → ServerConnectionMonitor + IScreenController → LVGL widgets
```

Session accumulation (distance, average consumption) and every derived value
(instantaneous consumption, fuel rate, boost, altitude) are computed
**server-side** and carried in the `Payload`; clients are pure renderers.

This vehicle answers neither the fuel-rate PID (0x5E) nor any boost PID, so the
server derives fuel rate from the free-running `CAN 0x608` injected-fuel broadcast
(with a MAF-based fallback) and boost from manifold minus barometric pressure.

The `Payload` struct is fixed-size and packed; a `static_assert` in
`lib/core/include/payload.h` enforces its exact byte size at compile time. Use
fixed-width integer types only in its fields — host arm64 and ESP32 sizes differ.

---

## Setup

**1. Clone and create the security config**

```bash
git clone git@github.com:fabioseidl/mitsubish-pajero-dashboard.git
cd mitsubish-pajero-dashboard
cp lib/core/include/security_config.h.example \
   lib/core/include/security_config.h
```

Edit `security_config.h` and set a real 16-byte PMK. Every node must carry the
same value. This file is gitignored — never commit it.

**2. Run host tests**

```bash
cd test && pio test -e native_tests
```

All tests must pass before flashing. They use Unity, compile with
`-DUNIT_TEST -std=c++17`, and mock the hardware drivers (CAN, display, ESP-NOW).

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

The OBD-II PID map (`lib/core/include/pid_map.h`) is hand-maintained — add new
PIDs manually with explicit formula parameters and an honest `verified` flag; do
not auto-generate it. New non-trivial logic is expected to arrive with host tests.

Project documentation lives in `.claude/` so that it loads on demand in a Claude
Code session, and is equally readable on its own:

- `CLAUDE.md` — architecture, commands and the repo-wide invariants
- `.claude/skills/server-firmware/` — CAN polling, deep sleep, derived-value maths
- `.claude/skills/client-firmware/` — the client contract, plus a hardware
  reference per display board under `references/`
- `.claude/skills/payload-protocol/` — the `Payload`/PID/ESP-NOW contract
- `.claude/skills/host-tests/` — test layout and conventions
- `.claude/skills/can-reverse-engineering/` — the sniffer and what is confirmed,
  rejected or still unverified on this vehicle

Raw reverse-engineering research stays with the tool that produced it, in
`projects/sniffer/assets/MITSUBISHI_ADVANCED_PIDS.md`.
