# CLAUDE.md

Guidance for Claude Code working in this repository.

Deeper context lives in `.claude/skills/` and loads on demand — this file holds
only what is true everywhere.

| Skill | Covers |
|---|---|
| `server-firmware` | `projects/server` + `server_emulator`: CAN, polling, deep sleep, derived maths |
| `client-firmware` | The four display clients, their shared contract and per-board quirks |
| `payload-protocol` | `Payload`, `pid_map.h`, ESP-NOW — the contract between them |
| `host-tests` | The Unity suites under `test/host` |
| `can-reverse-engineering` | `projects/sniffer`: finding and confirming new vehicle data |

Slash commands: `/build <project>`, `/flash <project>`, `/test`.

## What this is

A distributed dashboard for a Mitsubishi Pajero Dakar (Pajero IV 3.2 DI-D, 4M41
diesel). One ESP32 reads OBD-II data from the vehicle CAN bus and broadcasts a
packed `Payload` struct at 10 Hz over ESP-NOW; several ESP32 clients receive it
and render it with LVGL.

```
CAN bus → CANDriver → PIDDictionary + PIDTranslator → DataAggregator (mutex)
        → DerivedCalculator → SessionAccumulator → PayloadBuilder → ESPNowBroadcaster
                                    ↓ ESP-NOW broadcast, 10 Hz
ESP-NOW ISR → ESPNowReceiver → ServerConnectionMonitor + IScreenController → LVGL widgets
```

## Layout

```
lib/core/            Shared by every node: payload.h, pid_map.h, ESP-NOW, IDisplay,
                     brightness, connection monitor
projects/
  server/            Production server — ESP32-S3 + MCP2515, reads CAN, broadcasts
  server_emulator/   Same broadcast from synthetic data (ESP-IDF) — client dev without the car
  main_display/      Waveshare 7" 1024×600 RGB dashboard + GPS/IMU/environment sensors
  main_hud/          Guition 3.5" QSPI windshield HUD — speed only
  glass_display/     Waveshare ESP32-C6 1.47" — speed only
  client_simple_hud/ CYD 2.4" SPI HUD, LDR auto-brightness
  sniffer/           CAN sniffer + DBC + monitor.py for reverse engineering
test/host/           Unity host tests: test_server/, test_lib/, mocks/
ui/                  SquareLine Studio projects and source assets
```

## Commands

```bash
# Build / flash — from the sub-project directory
cd projects/<name> && pio run
cd projects/<name> && pio run --target upload

# Host tests — from the repo root
cd test && pio test -e native_tests
```

If `pio` is not on PATH it is at `~/.platformio/penv/bin/pio`.

**First-time setup** — `lib/core/include/security_config.h` is gitignored and
nothing builds without it:

```bash
cp lib/core/include/security_config.h.example lib/core/include/security_config.h
# then set a real 16-byte PMK — the same value on every node
```

## Invariants

- **`Payload` is a packed struct with a `static_assert` on its exact size**
  (currently 149 bytes; ESP-NOW's broadcast ceiling is 250). Change a field →
  update the assert, bump `PAYLOAD_VERSION`, update `PayloadBuilder` *and* the
  emulator, update the field table in `test/host/test_server/test_payload_coverage.cpp`
  (it fails until every byte is accounted for), and reflash every client. See the
  `payload-protocol` skill for the full checklist.
- **Only fields something can actually populate belong in `Payload`.** A field no
  code path writes still costs its width on all 10 broadcasts a second. Version 5
  removed 84 bytes of permanently-zero Mode 22 fields on exactly this ground.
- **Fixed-width types only** in `Payload` and in tests — never `int`, `long` or
  `size_t`. Host tests build for arm64; the firmware is 32-bit Xtensa.
- **`pid_map.h` is hand-maintained.** Add PIDs manually with explicit formula
  parameters and an honest `verified` flag. Never generate it.
- **Never commit `security_config.h`.**
- **Clients are pure renderers.** Session totals, consumption, boost and altitude
  are all computed server-side and carried in the `Payload`.
- **ESP-NOW is unidirectional** — broadcast only, no ACKs, no client→server messages.
- **LVGL is single-threaded.** Copy the payload in the ESP-NOW callback, apply it
  in `tick()`. Never touch a widget from the receive callback.
- **Pin numbers belong in `include/pin_config.h`**, never inline in code.
- **Generated files are never hand-edited** — SquareLine exports (`ui/`, `src/ui/`)
  and `lv_font_conv` output (`ui_font_*.c`). Adapt them from a bridge module.
- **Run the host tests before flashing.** They are the only verification that does
  not need the vehicle.

## Working on this repo

Library versions are pinned deliberately in several projects (a wrong LovyanGFX or
Arduino_GFX version means a dark panel, not a compile error). Read the relevant
board reference under `.claude/skills/client-firmware/references/` before bumping
one.

The calibration constants in `derived_calculator.cpp` encode real road-test
results. Each carries a comment explaining how to tune it — change them by that
method, not by guessing, and keep the reasoning in the comment.


## Code comments
- Simple and direct and techinical code notes. Never tell a history of hot it was and how it was now.
- Prefer single line comments to multline ones.
- Always add docstring to all classes and functions