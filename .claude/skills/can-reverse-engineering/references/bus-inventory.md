# Bus inventory

Every CAN ID observed on this vehicle, from `projects/sniffer/dumps/candump.log`
(51,278 frames; 2 corrupt lines from serial glitches skipped). Rates are
1/median inter-arrival — the log contains a clock-sync jump, so elapsed-span
rates are wrong.

**The whole bus is 14 IDs.** That is the complete search space.

| ID | Hz | frames | DLC | bytes that move | status |
|---|---|---|---|---|---|
| `0x212` | 45.8 | 6808 | 8 | 5 | decoded — Engine_Data (DBC) |
| `0x218` | 45.5 | 6155 | 8 | 6 | **confirmed** — gear, selector sweep |
| `0x308` | 45.5 | 5401 | 8 | 3 | decoded — Cooling_System (DBC) |
| `0x312` | 45.5 | 5730 | 8 | 4 | decoded — Throttle_Status (DBC) |
| `0x608` | 3.5 | 643 | 8 | 3 | **confirmed** — injected fuel qty, FUELLOG |
| `0x215` | 45.4 | 4787 | 8 | 6 | candidate (igkov), unconfirmed |
| `0x445` | 22.8 | 2780 | 7 | 4 | candidate (igkov), unconfirmed |
| `0x424` | 13.0 | 1964 | 8 | 4 | candidate (igkov), unconfirmed |
| `0x325` | 45.6 | 5632 | 2 | 0 | **unknown** |
| `0x418` | 45.3 | 4109 | 7 | 3 | **unknown** |
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

**Coverage:** 5 of 14 IDs decoded, 3 unconfirmed candidates, 6 unknown. The
unknowns are 36% of the bus by ID and include two continuous signals.

## Regenerating

Parse `(timestamp) iface ID#HEXDATA`, bucket by ID, rate from median
inter-arrival (not elapsed span — the clock-sync jump breaks that), and count
distinct values per byte position. Skip odd-length and non-hex data fields.
