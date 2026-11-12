# Bus inventory

Every CAN ID observed on this vehicle, from `projects/sniffer/dumps/candump.log`
(51,278 frames; 2 corrupt lines from serial glitches skipped). Rates are
1/median inter-arrival — the log contains a clock-sync jump, so elapsed-span
rates are wrong.

**The whole bus is 14 IDs.** That is the complete search space.

| ID | Hz | frames | DLC | bytes that move | status |
|---|---|---|---|---|---|
| `0x212` | 45.8 | 6808 | 8 | 5 | decoded — Engine_Data (DBC) |
| `0x218` | 45.5 | 6155 | 8 | 6 | **confirmed** — gear, selector sweep; D4 bit 0 = low range |
| `0x308` | 45.5 | 5401 | 8 | 3 | **confirmed** — engine rpm D1,D2 (DBC calls it Cooling_System) |
| `0x312` | 45.5 | 5730 | 8 | 4 | decoded — Throttle_Status (DBC) |
| `0x608` | 3.5 | 643 | 8 | 3 | **confirmed** — injected fuel qty (D5,D6), coolant temp (D0) |
| `0x215` | 45.4 | 4787 | 8 | 6 | **confirmed** — speed D0,D1 (1/128 km/h) |
| `0x445` | 22.8 | 2780 | 7 | 4 | **confirmed** — auto A/C incl. set point in D0 |
| `0x424` | 13.0 | 1964 | 8 | 4 | **confirmed** — ETACS low/high beam, turn signals, doors; D5 = minute counter (candidate) |
| `0x325` | 45.6 | 5632 | 2 | 0 | **unknown** |
| `0x418` | 45.3 | 4109 | 7 | 3 | **confirmed** — D6 selector lever position |
| `0x447` | 22.7 | 2588 | 3 | 0 | **unknown** |
| `0x317` | 18.3 | 2611 | 8 | 3 | **unknown** |
| `0x101` | 10.1 | 1126 | 1 | 0 | **unknown** |
| `0x415` | 5.4 | 942 | 7 | 0 | **unknown** |

## Distinct values per byte

A byte with 256 distinct values spans its full range — a continuous signal, or
the low half of a 16-bit one. A byte with 1 never moved in this capture.

```
0x212  [83,   1,   1,   1,   5, 256,   5, 256]
0x218  [16,  73,  18,   4,   3,   2,   1,   1]
0x308  [ 1,   6, 256,   1,   1,   2,   1,   1]
0x312  [ 5, 256,   5, 256,   1,   1,   1,   1]
0x608  [16,   1,   1,   1,   1,  11, 230,   1]
0x215  [18, 256, 102, 128, 102, 128,   1,   1]
0x445  [ 2,   1,   7,   2,   1,   2,   1]
0x424  [ 4,   3,   6,   1,   1,   4,   1,   1]
0x325  [ 1,   1]
0x418  [ 1,   1,   1,   1,   4, 256,   4]
0x447  [ 1,   1,   1]
0x317  [ 1,   1,   1,   7, 256,   2,   1,   1]
0x101  [ 1]
0x415  [ 1,   1,   1,   1,   1,   1,   1]
```

## What this says

**`0x236` is not on this bus.** It sits in the sniffer's `WATCH` candidate list
(steering angle, from the igkov/MPS2 map) and never appeared once in 51k frames.
Either it needs an ECU this truck lacks, or igkov's dash reads a different
platform. Drop it from the candidate list or mark it absent.

**`0x16F` is a frame to go looking for.** It carries the A/C compressor-clutch
state but is not among the 14 IDs captured here — the capture was mostly stationary
with A/C likely off. `WATCH 16F` with the compressor cycling will say whether the
OBD port exposes it. See `projects/sniffer/assets/MITSUBISHI_ADVANCED_PIDS.md §9.5`.

**Two unknown frames carry a full-range continuous signal** — the highest-value
targets left:
- `0x418` byte 5, 256 distinct at 45 Hz, with bytes 4 and 6 also moving (4 states
  each). A 45 Hz full-range value with low-variety neighbours looks like a
  16-bit measurement plus flags.
- `0x317` byte 4, 256 distinct at 18 Hz.

Use `SIG` or `DIFF` on these against throttle/speed on a drive.

**Four unknown frames never changed a single byte** in this capture: `0x325`
(2 bytes, 45.6 Hz), `0x447` (3 bytes), `0x415` (7 bytes), `0x101` (1 byte).
A frame broadcast 45 times a second that never varies is a status or heartbeat
word whose inputs were not exercised — the 4WD dial, centre diff lock, doors,
lights, handbrake. This is exactly what `ADIFF` is for: one toggle sweep while
the whole bus is watched. Note the capture was mostly stationary, so anything
speed-dependent would also look static here.

**Coverage:** 8 of 14 IDs decoded or confirmed, 6 unknown. The unknowns are 43%
of the bus by ID and include two continuous signals.

## 4WD dial sweep (2026-11-12)

Stationary, engine idling, live bus monitor plus a timestamped byte-change
recorder. Every dial position was visited in both directions
(4LLc → 4HLc → 4H → 2H → 4H → 4HLc → 4LLc, selector in N).

- **`0x218` D4 bit 0 (`0x01`) = low range (4LLc).** Cleared leaving 4LLc, set
  entering it, unchanged at every other position. Confirmed.
- **2H, 4H and 4HLc are identical on CAN.** No byte on any of the 14 IDs moved
  for those transitions, including the static `0x325`, `0x447`, `0x415`,
  `0x101`. The research lists the transfer-case ECU on CAN at `0x786` →
  `0x787` (KWP `0x21` LID `0x21`, D0 = traction mode), which this session
  never reached — see the KWP section below. Untested, not ruled out.
- **`0x418` D6 = selector lever**, one bit per position: `00` P, `08` R,
  `10` N, `20` D. Confirmed.
- **`0x218` D4 bit 6 (`0x40`)** toggles while the selector moves through
  R/N/D. Meaning unknown.
- **`0x424` D5 steps +1 exactly every 60 s** (`0x53` → `0x62` over ~15 min).
  Probably minutes since ignition on. Not a temperature, though it looks like
  one beside the warming coolant.
- `0x218` D0, D1, D5 alternate every frame (`88`/`08`, `1A`/`23`, `80`/`00`) —
  a frame toggle or check value, not state.
- **`0x308` D1,D2 = engine rpm**, confirmed against the tachometer. (An early
  1179 vs 775 mismatch with Mode 01 `0x0C` came from comparing readings taken
  at different moments of a cold idle.)
- **Position lamps (`0x424` D0 `0x04`) and mirror heater (`0x445` D4 `0x20`)**
  confirmed by toggling them.
- **None of the 11 unverified Mode 01 PIDs answer** (`VERIFY ALL` at idle).

## KWP 0x21 (ReadDataByLocalIdentifier) map

An `RDLI <req> 01` sweep of `0x700`–`0x7F7` found two responders, engine
`0x7E0` and TCM `0x7E1`. It only listened on request + 8, so ECUs that answer
on request + 1 (transfer case `0x786` → `0x787`, cluster, ABS; see
`pid_map.h` "Legacy KWP service 0x21") were not tested. Both responders
implement KWP `0x21`; every other LID returns NRC `0x12`.

- **TCM `0x7E1`:** LIDs `01`–`04`, `E0`, `E1`, `E4`.
- **Engine `0x7E0`:** 92 LIDs — `01`–`04`, `08`–`19`, `1E`, `21`–`29`, `40`–`5F`,
  `73`–`79`, `A0`–`AC`, `B0`–`BF`, `E0`, `E1`, `E4`, `FA`.

**TCM LID `04` follows the 4WD dial** (candidate, one stationary sweep in N):

| Position | `61 04` D0 D1 D2 D3 |
|---|---|
| 2H | `08 00 01 00` |
| 4H | `08 00 01 00` |
| 4HLc | `08 20 01 00` |
| 4LLc | `00 00 01 00` |

D0 `0x08` = high range, D1 `0x20` = centre diff locked in high range. 2H and 4H
are identical here and in every other LID read in both positions.

**Research candidates ruled out by this session's replies:** `7E0 21 1A`
(absolute load) answers NRC `0x12`; `7E0 21 02` has 5 data bytes, so the raw
gear code at D9 does not exist; `7E1 21 03` has 6 constant bytes
(`FF 00 00 00 FF 00`), so neither the OBDScanner ATF (D9 − 55) nor the igkov
odometer (D2..D4 = 0 km) applies. `7E1 21 02` D5 and D6 both climb slowly as the
engine warms — the ATF candidates (igkov: D6 − 40). `7E0 21 15` holds a constant
`00 1E 5B 40 28`, the "approx odometer" candidate.

**qhub.kz cross-check (TCM `7E1 21 02`).** Its 5AT layout fits every snapshot
above (stationary in N): D1D2 / 2 = input shaft rpm (630–882), D3D4 / 2 =
output shaft rpm (0), D5 − 40 = ATF °C (36–38, rising), D6 × 33.15 / 255 = TCM
volts (13.5–14.0), D7 × 2.8 = km/h (0), D8 − 51 = converter slip rpm (17–22), D9
gear bits (`01` = P/N; `02` R, `04`–`40` 1st–5th). So D6 is voltage, not the ATF
candidate igkov gave. Its `21 04` selector bits (D2 `01` = P) do not fit — we
read `01` in N. Its engine `21 01` rpm (D2D3 / 8) reads 1.33× the TCM input
shaft; check against `0x308`. `0x608` D1 (qhub: ambient − 40) is `00` throughout
`candump.log`; `WATCH` now prints it as `ambient=` for a thermometer comparison.

### Next session (sniffer firmware ready)

1. `ECUS` — every ECU on `0x600`–`0x7FF` with its real reply id; then
   `ECUS 01 600 7FF SESS` for modules that only answer inside a session.
2. `RDLI 786 21`, then `KWATCH 786 21` through 2H → 4H → 4HLc → 4LLc. If D0
   tracks the dial, 2H vs 4H is solved. Repeat with `SESS` if `786` is silent.
3. `IDENT` and `LIDS` on each new responder (`IDENT 6A0`, `LIDS 786`, ...).
4. Meter: `RDLI 6A0 A3` (fuel, D2 − 16 × 0.7 L) and `RDLI 6A0 AD` (odometer,
   3 bytes little-endian) against the dash; `79E AD` if `6A0` is silent.
5. `RDLI 7E1 02` while driving through the gears — confirms the D9 gear bits and
   shaft speeds. `KWATCH 7E0 01` beside `WATCH 308` settles the rpm scale.
6. `WATCH` with the A/C cycling — `0x16F` is in the default list. Note the
   `0x608` `ambient=` value against a thermometer at different times of day.

## Regenerating

Parse `(timestamp) iface ID#HEXDATA`, bucket by ID, rate from median
inter-arrival (not elapsed span — the clock-sync jump breaks that), and count
distinct values per byte position. Skip odd-length and non-hex data fields.
