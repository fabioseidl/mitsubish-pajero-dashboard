---
name: can-reverse-engineering
description: Discovering and confirming new vehicle data on the Pajero CAN bus with projects/sniffer — its serial modes (PASSIVE, WATCH, DIFF, ADIFF, SCAN, SIG, FUELLOG, RDLI), what is already confirmed or rejected on this vehicle, and the rule for promoting a candidate frame into pid_map.h. Use when asked to find a new CAN code, decode an unknown frame, add a Mitsubishi advanced PID, or judge whether a value can be trusted.
---

# CAN reverse engineering (`projects/sniffer`)

The sniffer runs on the **same hardware as the server** (ESP32-S3 + MCP2515 over
SPI, CAN A on the OBD-II port, identical pins). Flash it instead of the server
when hunting for data.

## What is already settled on this vehicle

Do not redo this work:

**Confirmed and in use**
- `CAN 0x608` D5,D6 — injected fuel quantity, a **mass flow in mg/s**. Confirmed by a
  road `FUELLOG`: a clean 0 during overrun (coasting in gear, off throttle, rpm
  2400→1800, speed > 0), ~310–340 at idle, ~4734 under hard acceleration. Airflow
  cannot read 0 while the engine spins, so this is fuel, not MAF.
- `CAN 0x608` D0 — coolant temperature, `(D0 - 40)` °C. Confirmed on this vehicle.
  `projects/server` takes coolant from here and no longer polls Mode 01 `0x05`:
  the frame runs at ~3.5 Hz against the ~0.04 Hz a slow round-robin slot gave it.
- `CAN 0x218` D2 — low nibble = current gear, high nibble = target gear. Confirmed
  by a full selector sweep (`P→R→N→D→1→2→3→2→D`): `0x0`=N, `0x1`=D stopped/manual 1,
  `0x2..0x4`=2nd–4th, `0x5`=5th (inferred from the pattern), `0xB`=R, `0xD`=P.
  Mixed nibbles appear mid-shift (`0xBD` = Park→Reverse).
- `CAN 0x215` D0,D1 — vehicle speed, 1/128 km/h. Confirmed on the live bus monitor
  against the dash speedometer. `projects/server` now takes speed from this frame
  and no longer polls Mode 01 `0x0D`: the broadcast runs at ~45 Hz against a poll
  slot's ~1.5 Hz, and dropping it speeds up every other fast PID. D2,D3 is a
  distance counter whose counts-per-km (`BCAST_TRIP_COUNTS_PER_KM`) is still
  MPS2's number — nothing uses it; session distance is integrated from speed.
- `CAN 0x424` — ETACS body control. Low beam (`D1 & 0x20`), high beam
  (`D1 & 0x04`), left and right turn signals (`D1 & 0x02`, `D1 & 0x01`) and the
  two door bits (`D2 & 0x02` driver, `D2 & 0x01` other) confirmed by a lamp,
  indicator and door sweep. Only the position-lamp bit is still MPS2's guess.
- `CAN 0x445` — auto A/C. Fan speed (`D5` high nibble), vent routing,
  recirculation and A/C-on track the panel. `D0` is the temperature set point,
  pinned by a dial sweep on this vehicle: `50` = LO, `64` = 20 °C then 2 counts
  per °C to `82` = 29 °C, `96` = 30 °C (the last click jumps 14 counts), `98` =
  HI. It reads like a blend-door target in half-degrees that saturates past the
  displayed range. `bcast_ac_set_temp()` reports what the panel shows; MPS2's
  `D0 - 63` offset was wrong for this truck.
- The Mode 01 PIDs marked `verified` in `pid_map.h`.

**Rejected — the ECUs do not implement UDS service 0x22.** A DID sweep answered
NRC `0x11` (serviceNotSupported) from the engine ECU and `0x80` from the TCM for
every DID. The documented Mitsubishi advanced PIDs for the 4M41 (fuel temp
`0x20F2`, fan duty `0x2151`, transmission speeds `0x20AB`) are therefore **not
available on this truck**, even though the tables list them. The polling path is
gated off (`POLL_MODE22 = false`) and the ISO-TP reassembly code is kept dormant.

**Unverified candidates** — `pid_map.h` still carries a block of frames decoded by
the independent igkov/MPS2 Pajero Sport 2 dash (0x236 steering angle, 0x308 rpm,
0x312 torque). That project reads 0x608 and 0x218 exactly as our own road tests
confirmed, which is why the rest of its map was worth testing — and 0x215, 0x424
and 0x445 have since been confirmed here. **Nothing in `projects/server` reads
what is left.** They exist so the sniffer's `WATCH` mode can decode them on a
drive. Note 0x236 has never appeared on this bus at all.

## Reference material

- `projects/sniffer/assets/MITSUBISHI_ADVANCED_PIDS.md` — the full
  reverse-engineering write-up: bus parameters, the UDS request/ISO-TP/response
  cycle step by step, per-engine DID tables for 12 Mitsubishi models (4M41 =
  shared by Pajero IV and Pajero Sport II), a consolidated DID list, and the igkov broadcast/KWP
  second source. Referenced by name from `pid_map.h` comments — keep it there.
- `projects/sniffer/dumps/` — `candump.log` (passive capture) and `server_log.txt`.
- `references/bus-inventory.md` — every ID on this bus from `candump.log`: 14
  total, 5 decoded, 3 candidates, 6 unknown, with rate and per-byte variety.
  Start here to pick a target. Note `0x236` is in the `WATCH` list but has never
  appeared on this bus.
- `projects/sniffer/pajero_dakar.dbc`, `mitsubishi_advanced.dbc` — DBC definitions.
- `references/mut3-database.md` — what the MUT-III SE service software yields.
  Short version: no scaling and no broadcast frames, only ECU addressing. On
  this vehicle every ECU but the engine is K-line, so `0x7E8` really is the only
  CAN diagnostic responder. Lists other-platform addresses as candidates only.

## Sniffer modes

Switch live over the serial monitor, one command per line. `PASSIVE`, `WATCH`,
`DIFF` and `ADIFF` are **listen-only** (zero bus risk); `SCAN`, `SIG`, `FUELLOG`
and `RDLI` transmit, which puts the MCP2515 into normal mode and makes the node
ACK bus traffic.

| Mode | Use it for |
|---|---|
| `PASSIVE` | Quiet by default; `DUMP` toggles candump-format logging of every frame |
| `WATCH [id ...]` | Decode known free-running frames into engineering values, rate-limited. No args watches the candidate list (608 218 215 308 312 236 424 445) |
| `DIFF [id]` | One noisy frame: learn the bytes that churn while an input is held still, then print only when a previously-stable byte changes. Default 218 |
| `ADIFF` | Whole-bus DIFF — finds an **unknown** discrete-state frame (4WD dial, diff lock) in one toggle sweep without knowing its ID |
| `OBD` | List the standard Mode 01 PIDs the engine ECU supports |
| `SCAN [eng\|tcm] [start end] [sess]` | Sweep UDS Mode 22 DIDs (default `2000 21FF`), log every `0x62`. `sess` opens an extended session (`10 03`) held with TesterPresent, for ECUs that gate Mode 22 |
| `SIG <req> <did>` | Poll one DID at 10 Hz alongside rpm/accel/speed, CSV per cycle — correlate a candidate against throttle and overrun while driving |
| `FUELLOG` | CSV of the passive 0x608 fuel and 0x218 gear against polled rpm/accel/speed — the drive test that confirmed 0x608 |
| `RDLI <req> <lid>` | Legacy KWP ReadDataByLocalIdentifier (service 0x21), e.g. `RDLI 7E1 02` = AT info, `RDLI 7E1 03` = odometer |
| `T<unix>` | Set the wall-clock base for timestamps |
| `STOP` / `PASV` | Return to passive logging |

`projects/sniffer/web/` is a browser view of the same stream: every ID with its
rate and per-byte variety, highlighting the moment a settled byte steps — `ADIFF`
on the whole bus at once. `--replay ../dumps/candump.log` runs it with no
hardware. It holds no decode maths (see its README for why); it finds candidates,
the promotion rule below still governs what gets trusted.

`monitor.py` drives all of this from the host: it syncs the clock on connect,
forwards stdin lines to the sniffer, and toggles local capture to `candump.log`
with its own host-side `LOG` command. The serial port is hardcoded at the top —
update `PORT` for your adapter.

## Promoting a discovery into the firmware

A candidate becomes a real payload field only after a capture **on this vehicle**
agrees with the decode. The sequence:

1. Find the frame/DID (`ADIFF` for unknown discrete states, `SCAN` for DIDs,
   `WATCH` for a suspected known frame).
2. Correlate it against something physical while driving (`SIG` or `FUELLOG`).
   A value that only looks plausible at idle is not confirmed.
3. Sanity-check the magnitude against physics before believing a formula. The
   fuel-rate model was wrong for a while because a plausible-looking per-stroke
   interpretation predicted ~74 L/h on an engine whose absolute ceiling is ~32 L/h.
4. Add it to `pid_map.h` — a `PID_MAP[]` row for a Mode 01 PID, or a `0xA0–0xBF`
   slot ID for a DID or broadcast frame, with the source of the decode in a comment.
5. Decode it in `can_rx_task` (broadcast frames are captured **before** the
   request/response filter) and add it to a poll list if it must be requested.
6. Then, and only then, add a `Payload` field — follow the full checklist in the
   `payload-protocol` skill, including the `static_assert` size and reflashing
   every client.

Keep the honest status in the comment: `verified`, unverified candidate, or
rejected with the NRC observed. The value of this file is that it records what the
*car* answered, not what a table on the internet claims.
