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
  `0x101`. The research's transfer-case ECU at `0x786` → `0x787` does not
  answer on this truck, and no KWP LID on the five CAN ECUs moves either — see
  the KWP section below.
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
- **Fog lamps (`0x424` D0 `0x08`)** confirmed on and off with low beam on;
  ETACS `7A0` LID `09` bit `0x08` and LID `20` D1 bit `0x10` follow it.
- **Position lamps (`0x424` D0 `0x04`) and mirror heater (`0x445` D4 `0x20`)**
  confirmed by toggling them.
- **None of the 11 unverified Mode 01 PIDs answer** (`VERIFY ALL` at idle).

## KWP 0x21 (ReadDataByLocalIdentifier) map

`ECUS` over `0x600`–`0x7FF` (2026-11-12, auto reply id, with and without a
`10 92` session) found five responders. Nothing answers at `0x786` (transfer
case), `0x6A0` (meter), `0x79E` (cluster) or `0x78C` (body). Every unsupported
LID returns NRC `0x12`. Part numbers from `IDENT` (`1A 87`):

| Request → reply | Module | Part no. | LIDs that answer |
|---|---|---|---|
| `7E0` → `7E8` | Engine | 1860C804 | 92: `01`–`04`, `08`–`19`, `1E`, `21`–`29`, `40`–`5F`, `73`–`79`, `A0`–`AC`, `B0`–`BF`, `E0`, `E1`, `E4`, `FA` |
| `7E1` → `7E9` | A/T (TCM) | 8631B279 | `01`–`04`, `E0`, `E1`, `E4` |
| `784` → `785` | ABS | 4670A605 | `20`, `30`, `43`, `E0`, `E1` |
| `7A0` → `7A1` | ETACS | 8637A721 | 42: `01`–`03`, `05`, `06`, `08`–`0A`, `0D`–`1D`, `20`–`22`, `24`, `25`, `30`, `32`, `41`, `44`, `4F`, `6C`, `6D`, `70`, `C0`, `E0`, `E1`, `E4` |
| `7A2` → `7A3` | unknown | 7820A270 | `10`–`12`, `E0`, `E1` |

ETACS `7A0` also returns the VIN on `1A 90`. ETACS LID `25` returns the same
bytes as the `0x424` broadcast frame, so its other LIDs likely mirror body state.

**4WD dial sweep over all 57 data LIDs (2026-11-12, selector in N).** 2H, 4H and
4HLc read identically in every LID of all five ECUs; the only LIDs that moved
were drifting values or leftovers from a short D/R roll (ETACS `14`, ABS `30`),
which did not revert. Only low range shows:

| Position | TCM `61 04` D0 D1 D2 D3 |
|---|---|
| 2H, 4H, 4HLc | `00 00 04 00` |
| 4LLc | `00 20 04 00` |

D0 `0x02` = foot brake pedal pressed (set while held, cleared on release).
D1 `0x20` = low range, in and out of 4LLc with `0x218` D4 bit 0. D2 = selector
(qhub: `01` P, `02` R, `04` N, `08` D; seen `01` in P, `04` in N). An earlier
sweep that read `08 20 01 00` as 4HLc was taken in P with the dial and the
snapshots one step apart; D0 `0x08` did not reproduce and its meaning is unknown.
**2H vs 4H is not reachable on CAN**; the 4WD controller is likely K-line only.

**Handbrake: not on CAN.** Two release/apply cycles (selector in P, no pedal)
moved no broadcast byte and no LID on the five ECUs. The bits that moved in
between (`7A2` LID `11` D0 `01`→`09`, ETACS LID `1D` D1 `E3`→`63`→`43`) did not
follow the lever back. The switch likely feeds only the cluster, like the
odometer.

**Mirror fold: not on CAN.** Folding and unfolding moved no broadcast byte (not
even transiently) and no LID; the fold switch likely drives the motors directly.

**ETACS LID `1D` D1 moves with unrelated actions** (`E3` → `63` → `43` → `4B` →
`2B` → `2F` across handbrake, fog and mirror tests) without following any one
control. Probably a running or last-event value; don't use it as a switch state.

**Research candidates ruled out:** `7E0 21 1A` (absolute load) answers NRC
`0x12`; `7E0 21 02` has 5 data bytes, so the raw gear code at D9 does not exist;
`7E1 21 03` has 6 constant bytes (`FF 00 00 00 FF 00`), so neither the
OBDScanner ATF (D9 − 55) nor the igkov odometer applies. `7E0 21 15` holds a
constant `00 1E 5B 40 28`, the "approx odometer" candidate. **The odometer is
not in any CAN LID:** with the dash at 191911 km (2026-11-12), no 2–4 byte
window in the 151 positive replies of the five ECUs decodes to it exactly
(BE/LE, BCD, km or miles, /1–/100). `0x608` D1 (qhub:
ambient − 40) reads `00` in `candump.log` and live, so this truck likely doesn't send it there.

**qhub.kz cross-check (TCM `7E1 21 02`).** Its 5AT layout fits every snapshot
(stationary): D1D2 / 2 = input shaft rpm (630–882), D3D4 / 2 = output shaft rpm
(0), D5 − 40 = ATF °C (rising with warm-up), D6 × 33.15 / 255 = TCM volts
(13.5–14.0), D7 × 2.8 = km/h, D8 − 51 = converter slip rpm, D9 gear bits (`01`
P/N, `02` R, `04`–`40` 1st–5th). So D6 is voltage, not the igkov ATF
candidate. Its engine `21 01` rpm (D2D3 / 8) reads 1.33× the TCM input shaft.

### Still open

1. `RDLI 7E1 02` while driving through the gears — confirms D9 gear bits and
   shaft speeds. `KWATCH 7E0 01` beside `WATCH 308` settles the rpm scale.
2. Identify `7A2` (7820A270): `KWATCH 7A2 11` while operating candidates.
3. ETACS: `KWATCH 7A0 <lid>` while toggling lights, doors, wipers.
4. `WATCH` with the A/C cycling for `0x16F`.

## Regenerating

Parse `(timestamp) iface ID#HEXDATA`, bucket by ID, rate from median
inter-arrival (not elapsed span — the clock-sync jump breaks that), and count
distinct values per byte position. Skip odd-length and non-hex data fields.
