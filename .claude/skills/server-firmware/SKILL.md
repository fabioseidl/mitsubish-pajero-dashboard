---
name: server-firmware
description: The CAN/OBD-II server node and its emulator — MCP2515 wiring, OBD poll scheduling, ISO-TP and Mode 22 handling, deep-sleep power management, and the derived fuel-rate/boost/altitude maths with their calibration knobs. Use when working in projects/server or projects/server_emulator, changing what the server polls or broadcasts, or tuning fuel consumption, boost or altitude readings.
---

# Server firmware (`projects/server`)

Reads OBD-II data from the vehicle CAN bus, translates it, derives the values the
ECU does not report, accumulates session totals, and broadcasts a `Payload` at
10 Hz over ESP-NOW. Clients are pure renderers — **all** derivation and session
maths happen here.

```
CAN bus → CANDriver → PIDDictionary + PIDTranslator → DataAggregator (mutex)
        → DerivedCalculator → SessionAccumulator → PayloadBuilder → ESPNowBroadcaster
```

## Hardware

| Item | Value |
|---|---|
| MCU | ESP32-S3-DevKitC-1 (`board = esp32-s3-devkitc-1`, Arduino framework) |
| CAN controller | **MCP2515 over SPI** (`-DUSE_MCP2515`, `autowp/arduino-mcp2515`) |
| Bus speed | 500 kbps, OBD-II port |
| Vehicle | Mitsubishi Pajero Dakar / Pajero IV 3.2 DI-D (4M41, diesel) |

The built-in TWAI controller is **not** used, despite what a leftover
`"TWAI initialized successfully"` log line in `main.cpp` claims. Pins live in
`include/pin_config.h` (SPI 11/12/13, CS 10, RST 9, INT 8) — never inline a raw
GPIO number.

## FreeRTOS tasks

| Task | Core | Priority | Stack | Touches |
|---|---|---|---|---|
| `can_rx_task` | 1 | 5 | 4096 | `CANDriver`, `PIDDictionary`, `PIDTranslator`, `DataAggregator` |
| `broadcast_task` | 0 | 3 | 4096 | `DerivedCalculator`, `SessionAccumulator`, `PayloadBuilder`, `ESPNowBroadcaster` |

`DataAggregator` is the **only** shared state between the tasks and is
mutex-protected internally. Do not add a second shared object.

## Poll scheduling — read before adding a PID

The bus is polled one request at a time every `OBD_POLL_INTERVAL_MS` (50 ms). A
flat round-robin over ~60 PIDs would refresh each value only once per ~3 s, which
is visible as client lag. Instead `can_rx_task` keeps two lists:

- `FAST_PIDS` — the live dashboard values (RPM, speed, MAF, engine load, both
  accelerator pedals, MAP). Polled **every** cycle.
- `SLOW_PIDS` — everything else, drip-polled **one per cycle**, round-robin.

So a fast PID refreshes every `(FAST_COUNT + 1)` polls. **Every PID added to
`FAST_PIDS` slows down all the others** — add there only when staleness is
actually visible, and say why in a comment (the pedals are there for
overrun-detection timing, MAP because a slow-polled boost gauge lagged ~11 s).

## OBD-II protocol

- Requests go out on `0x7DF` (functional), responses come from `0x7E8` (engine)
  and `0x7E9` (TCM). Frames from any other ID are ignored.
- Mode 01 response: `data[1] == 0x41`, PID at `data[2]`, data from `data[3]`.
- PID `0x01` is a 4-byte bitmask — use `PIDTranslator::extractMilStatus()` and
  `extractDtcCount()`; never pass it to `translate()`.
- ISO-TP single (`0x0n`), first (`0x1n`) and consecutive (`0x2n`) frames are all
  handled, with a flow-control reply and per-ECU reassembly buffers.

### Mode 22 is disabled on purpose

`POLL_MODE22 = false`. A DID sweep with `projects/sniffer` proved this vehicle's
ECUs do not implement UDS service 0x22: the engine answers every DID with NRC
`0x11` (serviceNotSupported), the TCM with `0x80`. Polling them burned a
slow-round-robin slot per cycle on guaranteed rejections. The request builder,
ISO-TP reassembly and `MODE22_ADVANCED_PIDS` table are kept in place but dormant,
for a future vehicle that does answer. Do not re-enable without a fresh sniff
showing positive responses.

### Passive broadcast frames (this is where the good data comes from)

The 4M41 free-runs two frames that need no request, captured before the
request/response filter:

- `CAN 0x608`, `D5<<8|D6` → raw injected fuel in **mg/s** → slot `PID_BCAST_FUEL_RAW`.
  Reads exactly 0 during deceleration fuel cut-off.
- `CAN 0x218`, `D2` → gear: low nibble = current, high nibble = target.
  Codes `0x0`=N, `0x1..0x5`=forward, `0xB`=R, `0xD`=P (`GEAR_CODE_*` in `pid_map.h`).

## Power management — the board is on always-on OBD power

It must sleep or it drains the car battery. "Car off" is inferred from **CAN
silence**: no frame for `CAR_OFF_TIMEOUT_MS` (5 s) → `enterDeepSleep()`, which
drains the MCP2515 so its INT line goes idle, stops WiFi, then arms two wake
sources — GPIO 8 (MCP2515 INT, RTC-capable, `ESP_EXT1_WAKEUP_ANY_LOW`) and a
`DEEP_SLEEP_FALLBACK_S` (30 s) timer. Wake reboots the chip and re-runs `setup()`.

`g_car_on` gates the radio: `broadcast_task` holds off `broadcaster.begin()` until
CAN traffic is seen, so a timer probe with the car still off stays cheap (CAN only,
no ~150 mA radio). Keep any new work in `broadcast_task` behind that same gate.

## Derived values (`DerivedCalculator`)

This vehicle answers neither the fuel-rate PID (0x5E) nor any boost PID, so both
are derived. The calibration constants are at the top of `derived_calculator.cpp`,
each with a comment on how to tune it against the server's TX log.

**Fuel rate**, in strict preference order:
1. `PID_BCAST_FUEL_RAW` (CAN 0x608) — the real ECU figure. `L/h = raw * 3.6 * FUEL_RAW_TRIM / 835`.
   It is a **mass flow in mg/s, so the conversion has no rpm term** — an earlier
   rpm-multiplied model predicted ~74 L/h where the engine's absolute ceiling is
   ~32 L/h. Fix gain errors with `FUEL_RAW_TRIM`, never by reintroducing rpm.
2. Direct `PID_FUEL_RATE` when it reads > 0 (it never does on this car).
3. Overrun fuel cut-off → exactly 0: released pedal **and** rpm > 1100 **and**
   moving **and** a *valid* engine load ≤ 25%. The load term is what separates
   real DFCO from cruise control, where the physical pedal also reads ~0. When a
   guard PID is unavailable the code errs toward *not* cutting.
4. MAF fallback with a load-corrected diesel AFR — a line between an idle point
   (MAF 22 g/s, AFR 110) and a cruise point (MAF 60, AFR 37), clamped at AFR 22.

**Boost** (`computeBoostBar`) = `(MAP 0x0B − baro 0x33) / 100` bar, gauge pressure,
clamped at 0 (a diesel has no throttle plate, so off-boost MAP ≈ ambient and
rounding would otherwise show "-0.1"). Falls back to 101.3 kPa if 0x33 is missing.
`payload.boost_pres` is filled from this, **not** from the dead `PID_M22_BOOST_PRES` slot.

**Altitude** = international barometric formula against
`ALTITUDE_SEA_LEVEL_REF_KPA` (102.0, tuned locally — ~0.1 kPa ≈ 8 m). OBD baro is
whole-kPa so raw altitude snaps in ~84 m steps; `broadcast_task` applies an EMA
(α = 0.05) to smooth the stepping. The EMA cannot add real resolution.

## `PAYLOAD_FLAG_DATA_VALID`

Set from `DataAggregator::allRequiredPidsReceived()`, which requires **only RPM and
speed**. `PID_FUEL_RATE` is deliberately excluded: this ECU never answers it, and
since the flag gates the clients' rendering, requiring it would blank every screen
forever. Keep the required set to PIDs this vehicle actually answers.

# Server emulator (`projects/server_emulator`)

Produces a byte-identical broadcast with no CAN hardware, so clients can be
developed on the bench. Flash it in place of the real server.

- **ESP-IDF framework** (not Arduino), `platformio/espressif32@6.13.0`, LilyGo
  T-2CAN (ESP32-S3, 16 MB flash, 8 MB PSRAM), logging over native USB-Serial-JTAG.
- Entry point is `app_main()`, not `setup()/loop()`.
- `build_src_filter` compiles `projects/server/src/session_accumulator.cpp` and
  `lib/core/src/espnow_broadcaster.cpp` straight out of the other trees — it
  shares the real implementations rather than copying them. Keep it that way.
- `SimulationDataGenerator` drives sinusoidal `DrivingProfile::IDLE | CITY | HIGHWAY`
  off `elapsed_ms_`. `setProfile()` must not reset the session accumulator.
- Simulation invariants: RPM and speed never negative; fuel rate proportional to
  RPM and load (never independent noise); `mil_on` false; `dtc_count` 0;
  `PAYLOAD_FLAG_DATA_VALID` always set; `PAYLOAD_FLAG_ENGINE_RUNNING` iff RPM > 400.
- Never redefine `Payload` here and never reimplement ESP-NOW here.

# Legacy

`projects/server-prototype` (the earlier Arduino/esp32dev prototype) was removed.
It duplicated `projects/server`'s class layout against an older `Payload` and was
a standing source of confusion about which tree was real. Recover it from git
history if a decision needs to be archaeology'd:
`git log --diff-filter=D -- projects/server-prototype`.
