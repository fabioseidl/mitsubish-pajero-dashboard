# Mitsubishi Advanced OBD-II PIDs — Reverse-Engineering


---

## 1. Background — what these parameters are

The "advanced data" is simply a set of **manufacturer-specific UDS PIDs** (UDS Service `0x22`,
*ReadDataByIdentifier*) sent to specific ECU CAN addresses. To read them on an ESP32 we send the
UDS requests on the CAN bus directly and apply the equations in §4.

These reads need **no diagnostic session (`10 xx`) and no TesterPresent (`3E 00`)** — you send the
`22 xx xx` request and the ECU answers.

---

## 2. Bus & protocol parameters

All listed models (2008+) use **ISO 15765-4 CAN, 11-bit IDs, 500 kbit/s**.
(Older ECUs may use legacy ISO-9141 / ISO-14230 KWP or 29-bit CAN, but every model in the PID tables
below uses 11-bit CAN.)

| Item | Value |
|------|-------|
| Physical layer | CAN 2.0A, **500 kbit/s** |
| Addressing | 11-bit |
| Transport | **ISO-TP / ISO 15765-2** (single + multi-frame) |
| Application | **UDS Service 0x22** (ReadDataByIdentifier), 2-byte DID |
| Engine ECU request ID | `0x7E0` → response `0x7E8` |
| Transmission (TCM) request ID | `0x7E1` → response `0x7E9` |
| OBD-II connector pins | 6 = CAN-H, 14 = CAN-L, 16 = +12 V, 4/5 = GND |

> Response ID = request ID + 8 (standard ISO 15765-4 physical addressing).

---

## 3. The request / response cycle (step by step)

For a single parameter (example: **Fuel Temperature**, DID `0x20F2`, on the engine ECU):

### Step 1 — Build the UDS request
UDS payload = `22 20 F2` (Service `0x22` + DID high `0x20` + DID low `0xF2`) → 3 bytes.

### Step 2 — Wrap in an ISO-TP Single Frame
```
CAN ID : 0x7E0
Data   : 03 22 20 F2 00 00 00 00
         ^^                       PCI: Single Frame (0x0) | length = 3
            ^^ ^^ ^^              UDS service + DID
                     ^^...        padding (0x00 or 0xAA/0x55, any)
```

### Step 3 — Receive the response on `0x7E8`
The response may be a **Single Frame** (data ≤ 7 bytes) or a **Multi-Frame**.
For DIDs whose data extends to byte `E`/`L`, the ECU answers with a **First Frame**, so you must
do ISO-TP flow control:

```
RX 0x7E8 : 10 0B 62 20 F2 D0 D1 D2     <- First Frame (0x1), total length 0x00B = 11 bytes
TX 0x7E0 : 30 00 00 00 00 00 00 00     <- Flow Control: Clear-To-Send, block size 0, ST 0
RX 0x7E8 : 21 D3 D4 D5 D6 D7 D8 ..     <- Consecutive Frame #1 (0x2 | seq 1)
RX 0x7E8 : 22 D9 ..                    <- Consecutive Frame #2, etc.
```

### Step 4 — Reassemble and strip the header
Reassembled UDS response = `62 20 F2 D0 D1 D2 D3 D4 ...`
- `62` = positive response to service `22`
- `20 F2` = echoed DID
- `D0 D1 D2 ...` = **the data payload**

A **negative** response looks like `7F 22 xx` (`xx` = NRC). Treat it as "not supported / retry".

### Step 5 — Map data bytes to equation letters and apply the equation
The standard OBD-PID equation letters (A, B, C, …) map to the data payload **after** the 2-byte DID
echo:

| Letter | A | B | C | D | E | F | … | L |
|--------|---|---|---|---|---|---|---|---|
| Reassembled index | `resp[3]` | `resp[4]` | `resp[5]` | `resp[6]` | `resp[7]` | `resp[8]` | … | `resp[14]` |
| Data byte | D0 | D1 | D2 | D3 | D4 | D5 | … | D11 |

For Fuel Temperature: `value = E − 40 = resp[7] − 40` → result in °C.

### Step 6 — (optional) Poll loop
Repeat per parameter. Engine PIDs go to `0x7E0`, transmission PIDs to `0x7E1`. You can interleave
several DIDs in a round-robin at ~5–20 Hz.

---

## 4. Master PID table (all 12 models)

Notation:
- **Req** = the 3 UDS bytes you send (`22` + DID).
- **Bytes** = data bytes used (D0 = first byte after the DID echo = Torque `A`).
- **Formula** = applied to the raw integer to get the engineering value.
- `SIGNED16(x)` = interpret the 16-bit value as two's-complement signed.

### 4B11 — ASX / RVR 2.0 CVT
| Parameter | ECU | Req | Bytes | Formula | Unit | Range |
|-----------|-----|-----|-------|---------|------|-------|
| Cooling Fan Duty | 0x7E0 | `22 20 B7` | D0 | `D0 × 0.3922` | % | 0–100 |
| Idle Control | 0x7E0 | `22 20 C6` | D0 | `(D0 & 0x80) ≠ 0` | bool | 0/1 |
| Primary Speed | 0x7E1 | `22 20 AC` | D0 | `D0 × 32` | rpm | 0–7000 |
| Secondary Speed | 0x7E1 | `22 20 AC` | D1 | `D1 × 64` | rpm | 0–7000 |

### 4N13 — ASX 2.2 AT
| Parameter | ECU | Req | Bytes | Formula | Unit | Range |
|-----------|-----|-----|-------|---------|------|-------|
| Fuel Temperature | 0x7E0 | `22 20 F2` | D4 | `D4 − 40` | °C | −40–145 |
| Cooling Fan Duty | 0x7E0 | `22 21 51` | D1 | `D1` | % | 0–100 |
| Input Speed | 0x7E1 | `22 20 AB` | D1,D2 | `(D1×256 + D2) × 0.5` | rpm | 0–7000 |
| Output Speed | 0x7E1 | `22 20 AB` | D3,D4 | `(D3×256 + D4) × 0.5` | rpm | 0–7000 |

### 4A90/91 — Colt 1.3 / 1.5
| Parameter | ECU | Req | Bytes | Formula | Unit | Range |
|-----------|-----|-----|-------|---------|------|-------|
| Alternator Load | 0x7E0 | `22 20 AA` | D4 | `D4 × 0.392` | % | 0–100 |
| EVAP Purge Duty | 0x7E0 | `22 20 B9` | D0 | `D0 × 0.3922` | % | 0–100 |
| Input Speed | 0x7E0 | `22 20 DF` | D1,D2 | `(D1×256 + D2) × 1.0` | rpm | 0–7000 |
| Output Speed | 0x7E0 | `22 20 DF` | D3,D4 | `(D3×256 + D4) × 1.0` | rpm | 0–7000 |

*(Note: Colt reads both speeds from the engine ECU `0x7E0`, not the TCM.)*

### 4D56 — L200 IV 2.5 DI-D
| Parameter | ECU | Req | Bytes | Formula | Unit | Range |
|-----------|-----|-----|-------|---------|------|-------|
| Fuel Temperature | 0x7E0 | `22 20 F2` | D4 | `D4 − 40` | °C | −40–145 |
| Cooling Fan Duty | 0x7E0 | `22 21 51` | D1 | `D1` | % | 0–100 |
| Input Speed | 0x7E1 | `22 20 AB` | D1,D2 | `(D1×256 + D2) × 0.5` | rpm | 0–7000 |
| Output Speed | 0x7E1 | `22 20 AB` | D3,D4 | `(D3×256 + D4) × 0.5` | rpm | 0–7000 |

### 4A91/92 — Lancer VIII 1.5 / 1.6 AT
| Parameter | ECU | Req | Bytes | Formula | Unit | Range |
|-----------|-----|-----|-------|---------|------|-------|
| Alternator Load | 0x7E0 | `22 20 AA` | D4 | `D4 × 0.392` | % | 0–100 |
| EVAP Purge Duty | 0x7E0 | `22 20 B9` | D0 | `D0 × 0.3922` | % | 0–100 |
| Input Speed | 0x7E1 | `22 20 AB` | D1,D2 | `(D1×256 + D2) × 0.5` | rpm | 0–7000 |
| Output Speed | 0x7E1 | `22 20 AB` | D3,D4 | `(D3×256 + D4) × 0.5` | rpm | 0–7000 |

### 4B11/12 — Lancer VIII 2.0 / 2.4 CVT
| Parameter | ECU | Req | Bytes | Formula | Unit | Range |
|-----------|-----|-----|-------|---------|------|-------|
| Cooling Fan Duty | 0x7E0 | `22 20 B7` | D0 | `D0 × 0.3922` | % | 0–100 |
| Idle Control | 0x7E0 | `22 20 C6` | D0 | `(D0 & 0x80) ≠ 0` | bool | 0/1 |
| Primary Speed | 0x7E1 | `22 20 AC` | D0 | `D0 × 32` | rpm | 0–7000 |
| Secondary Speed | 0x7E1 | `22 20 AC` | D1 | `D1 × 64` | rpm | 0–7000 |

### 4B11/12 — Lancer VIII 2.0 / 2.4 SST (dual-clutch)
| Parameter | ECU | Req | Bytes | Formula | Unit | Range |
|-----------|-----|-----|-------|---------|------|-------|
| Cooling Fan Duty | 0x7E0 | `22 20 B7` | D0 | `D0 × 0.3922` | % | 0–100 |
| Idle Control | 0x7E0 | `22 20 C6` | D0 | `(D0 & 0x80) ≠ 0` | bool | 0/1 |
| Clutch 1 Slip Speed | 0x7E1 | `22 21 4F` | D0,D1 | `SIGNED16(D0×256 + D1) × 0.5` | rpm | −900–1500 |
| Clutch 2 Slip Speed | 0x7E1 | `22 21 4F` | D2,D3 ⚠ | `SIGNED16(D2×256 + D3) × 0.5` | rpm | −900–1500 |

### 4B11/12 — Outlander II 2.0 / 2.4 CVT
| Parameter | ECU | Req | Bytes | Formula | Unit | Range |
|-----------|-----|-----|-------|---------|------|-------|
| Cooling Fan Duty | 0x7E0 | `22 20 B7` | D0 | `D0 × 0.3922` | % | 0–100 |
| Idle Control | 0x7E0 | `22 20 C6` | D0 | `(D0 & 0x80) ≠ 0` | bool | 0/1 |
| Primary Speed | 0x7E1 | `22 20 AC` | D0 | `D0 × 32` | rpm | 0–7000 |
| Secondary Speed | 0x7E1 | `22 20 AC` | D1 | `D1 × 64` | rpm | 0–7000 |

### 4HN — Outlander II 2.2 DI-D
| Parameter | ECU | Req | Bytes | Formula | Unit | Range |
|-----------|-----|-----|-------|---------|------|-------|
| Fuel Temperature | 0x7E0 | `22 21 6B` | D11 | `D11 − 40` | °C | −40–145 |
| Fuel Rail Valve Duty | 0x7E0 | `22 21 6B` | D4 | `D4 × 0.3922` | % | 0–100 |
| Input Speed | 0x7E1 | `22 20 AB` | D1,D2 | `(D1×256 + D2) × 0.5` | rpm | 0–7000 |
| Output Speed | 0x7E1 | `22 20 AB` | D3,D4 | `(D3×256 + D4) × 0.5` | rpm | 0–7000 |

*(Fuel Temperature and Fuel Rail Valve Duty share DID `0x216B`; they are two values inside one response.)*

### 4N14 — Outlander II 2.3 DI-D
| Parameter | ECU | Req | Bytes | Formula | Unit | Range |
|-----------|-----|-----|-------|---------|------|-------|
| Fuel Temperature | 0x7E0 | `22 20 F2` | D4 | `D4 − 40` | °C | −40–145 |
| Cooling Fan Duty | 0x7E0 | `22 21 51` | D1 | `D1` | % | 0–100 |
| Clutch 1 Slip Speed | 0x7E1 | `22 21 4F` | D0,D1 | `SIGNED16(D0×256 + D1) × 0.5` | rpm | −900–1500 |
| Clutch 2 Slip Speed | 0x7E1 | `22 21 4F` | D2,D3 ⚠ | `SIGNED16(D2×256 + D3) × 0.5` | rpm | −900–1500 |

### 4M41 — 3.2 DI-D (Pajero IV V80 and Pajero Sport II KH8W share this engine)
| Parameter | ECU | Req | Bytes | Formula | Unit | Range |
|-----------|-----|-----|-------|---------|------|-------|
| Fuel Temperature | 0x7E0 | `22 20 F2` | D4 | `D4 − 40` | °C | −40–145 |
| Cooling Fan Duty | 0x7E0 | `22 21 51` | D1 | `D1` | % | 0–100 |
| Input Speed | 0x7E1 | `22 20 AB` | D1,D2 | `(D1×256 + D2) × 0.5` | rpm | 0–7000 |
| Output Speed | 0x7E1 | `22 20 AB` | D3,D4 | `(D3×256 + D4) × 0.5` | rpm | 0–7000 |

> ⚠ **Clutch 2 byte offset is an inference.** Clutch 1 and Clutch 2 carry an identical decoded formula,
> and the byte-offset that distinguishes them is not recoverable from the source. Clutch 1 = bytes D0,D1
> is certain; Clutch 2 almost certainly uses the next pair (D2,D3) of the same `0x214F` response.
> **Verify with a raw capture before trusting Clutch 2.**

### Consolidated unique DID list (for quick implementation)
| DID | ECU | Meaning | Data layout (observed) |
|-----|-----|---------|------------------------|
| `0x20AA` | 0x7E0 | Alternator load | D4 = load% raw |
| `0x20AB` | 0x7E1 | Trans speeds | D1,D2 = input; D3,D4 = output |
| `0x20AC` | 0x7E1 | CVT speeds | D0 = primary; D1 = secondary |
| `0x20B7` | 0x7E0 | Fan duty (petrol) | D0 = fan duty raw |
| `0x20B9` | 0x7E0 | EVAP purge duty | D0 = purge raw |
| `0x20C6` | 0x7E0 | Idle control flag | D0 bit7 |
| `0x20DF` | 0x7E0 | Colt trans speeds | D1,D2 = input; D3,D4 = output |
| `0x20F2` | 0x7E0 | Fuel temp (diesel) | D4 = temp+40 |
| `0x2151` | 0x7E0 | Fan duty (diesel) | D1 = fan duty |
| `0x214F` | 0x7E1 | Clutch slip (DCT) | D0,D1 = clutch1; D2,D3 = clutch2 (⚠) |
| `0x216B` | 0x7E0 | Fuel temp + rail valve | D4 = rail valve; D11 = fuel temp+40 |

---

## 5. Reference algorithm (pseudocode)

```
CONST CAN_BITRATE = 500000

struct Pid { uint16 reqId; uint16 did; }

function readAdvancedPid(reqId, did) -> dataBytes[] or error:
    respId = reqId + 8

    # --- send request (ISO-TP single frame) ---
    canSend(reqId, [0x03, 0x22, did>>8, did&0xFF, 0,0,0,0])

    # --- receive (ISO-TP) ---
    frame = canRecv(respId, timeout=200ms)
    pci   = frame[0] & 0xF0

    if pci == 0x00:                      # Single Frame
        len = frame[0] & 0x0F
        uds = frame[1 .. len]
    elif pci == 0x10:                    # First Frame
        total = ((frame[0]&0x0F)<<8) | frame[1]
        uds   = frame[2..7]              # first 6 UDS bytes
        canSend(reqId, [0x30,0x00,0x00,0,0,0,0,0])   # Flow Control CTS
        seq = 1
        while len(uds) < total:
            cf = canRecv(respId, timeout=200ms)       # 0x2_ consecutive
            uds += cf[1..7]
        uds = uds[0..total]
    else:
        return error

    if uds[0] == 0x7F: return error      # negative response 7F 22 NRC
    assert uds[0] == 0x62 and uds[1]==did>>8 and uds[2]==did&0xFF
    return uds[3..]                      # D0,D1,D2,... (D0 == Torque 'A')

# --- apply per-parameter formula ---
data = readAdvancedPid(0x7E0, 0x20F2)
fuelTempC = data[4] - 40                 # E - 40
```

`SIGNED16(hi,lo)`:
```
raw = (hi<<8) | lo
if raw >= 0x8000: raw -= 0x10000
return raw
```

---

## 6. Two implementation paths on ESP32

### Path A — Native CAN (recommended)
- ESP32 built-in **TWAI** controller + a CAN transceiver (SN65HVD230 / TJA1050 / MCP2551).
- Implement ISO-TP (single + first/consecutive + flow control) as in §5.
- Fastest, most reliable, no extra module. Bus at 500 kbit/s.

### Path B — ELM327 front-end (quickest to prototype)
Mirror exactly what Torque does, over an ELM327 (UART/Bluetooth):
```
ATZ            reset
ATE0           echo off
ATSP6          protocol 6 = ISO 15765-4 CAN 11/500
ATCAF1         CAN auto-formatting ON  (ELM builds ISO-TP for you)
ATSH 7E0       set request header to engine ECU   (use 7E1 for TCM)
ATFCSH 7E0     (optional) flow-control header
22 20 F2       send the UDS request; ELM returns "62 20 F2 D0 D1 ..."
```
Parse the hex text after `62 20 F2`. With `ATCAF1`, the ELM handles multi-frame reassembly and flow
control automatically — simpler, but slower than native TWAI.

---

## 7. Validation checklist (do this before trusting values)

1. **Confirm bus speed/protocol** by reading a standard PID first (e.g. `01 0C` engine RPM on `0x7E0`).
2. For each advanced DID, **log the full reassembled response** once and verify the byte the formula
   uses is present and plausible (e.g. Fuel Temp `D4` ≈ ambient+40 on a cold start).
3. **Clutch 2 (DID 0x214F):** capture the full `0x214F` response and confirm whether clutch 2 is at
   D2,D3 (assumed) or elsewhere.
4. If a DID returns `7F 22 31` (requestOutOfRange) or `7F 22 12`, the parameter may not exist on that
   trim — skip it.
5. Engine must be **running** for most values.
6. These are **read-only** UDS reads. Do **not** send writes to the TCM.

---

## 8. Second source — igkov `bcomp11` (passive broadcast channels + legacy KWP)

> Source: <https://github.com/igkov/bcomp11> — an open-source Pajero trip computer
> (`obd.c` request/ISO-TP, `bcomp.c` decoding). This source is **independent of the
> UDS `0x22` table above** and contributes two things that table doesn't have: data
> read **passively from free-running broadcast frames** (no request at all), and a
> **legacy KWP service `0x21`** path for the transmission/odometer.

### 8.1 Free-running broadcast frames (no request needed) — **confirmed on this truck**

Both IDs below are already present in `dumps/candump.log` on our Pajero Sport II (0x608 at
~5–10 Hz, 0x218 at ~50 Hz). Because they are broadcast continuously, the sniffer reads
them in **listen-only** mode — zero bus risk.

| CAN ID | Meaning (bcomp11) | Bytes | Formula | Notes |
|--------|-------------------|-------|---------|-------|
| `0x608` | Diesel injected fuel quantity | D5,D6 | `D5×256 + D6` (raw, 16-bit) | bcomp11 `bcomp.raw_fuel`. **CONFIRMED real injected fuel** by a road `FUELLOG`: snaps to a clean **0** during overrun (coast in gear, off throttle, rpm 2400→1800, speed>0), ~310–340 at idle, up to ~4734 under hard accel. Airflow can't read 0 while the engine spins, so this is fuel, not MAF. |
| `0x218` | Transmission gear / state | D2 | `D2 & 0x0F` = current gear, `D2 >> 4` = target gear | **Confirmed** by a full selector sweep on the 4M41 (see code map below). D2 packs target gear (high nibble) and current gear (low nibble) — equal at rest, differ only mid-shift. |

**`0x218` D2 gear codes (confirmed on the Pajero Sport II 4M41):**

| Nibble | Meaning | | Nibble | Meaning |
|--------|---------|-|--------|---------|
| `0x0`  | Neutral (N) | | `0x4` | 4th gear |
| `0x1`  | Drive (stopped) / Manual 1 | | `0x5` | 5th gear |
| `0x2`  | 2nd gear | | `0xB` | Reverse (R) |
| `0x3`  | 3rd gear | | `0xD` | Park (P) |

> Verified by sweeping `P→R→N→D→manual 1→2→3→2→D` and reading `D2`: each dwell landed
> on a clean code, and shifts showed mixed nibbles (e.g. `0xBD` = Park→Reverse, `0x21` = 2→1).
> `0x5` (5th) is inferred from the 1–4 pattern; confirm on a road drive.

> `0x608` is now **fully confirmed** as injected fuel (D5,D6 location + overrun→0 behaviour,
> see the table note). The only thing left to nail is the **raw→L/h scale**:
> `fuel_L_per_h ≈ raw × rpm × K` (raw reads like a per-stroke injection quantity, so it scales
> with injection frequency ∝ rpm). Solve `K` from one anchor — best a tank-average economy, or
> idle `raw≈325 @ ~650 rpm` against a known idle figure (~0.85 L/h). Then `0x608` can **replace
> the MAF→AFR estimate** in `projects/server/src/derived_calculator.cpp`.
> Note **D0 of `0x608` is a separate slow byte** (≈`0x4D`/`0x7A`, possibly a temperature) — not load.

### 8.2 Legacy KWP `0x21` ReadDataByLocalIdentifier (TCM)

bcomp11 reads the Pajero auto-transmission and odometer with the **older KWP service
`0x21`** (1-byte local identifier), not UDS `0x22`. Request = `02 21 LID`, positive
response = `61 LID D0 D1 …` (note: **one** echo byte, so D0 = `resp[2]`).

| Req | LID | Meaning | Candidate decode (D0 = `resp[2]`) |
|-----|-----|---------|-----------------------------------|
| `0x7E1` | `0x02` | AT info | input = `D2×128 + D3/2` rpm · output = `D4×128 + D5/2` rpm · ATF = `D6 − 40` °C · relay = `D7×3315/255` · ratio = `D8×714/255` · slip = `D9 − 51` |
| `0x7E1` | `0x03` | Odometer | `(D2×256 + D3)×256 + D4` km (24-bit) |

> ⚠ These LID decodes are bcomp11's and may target an **earlier Pajero generation**.
> The well-researched §4 table reads our 4M41 trans speeds via **UDS `0x22` DID `0x20AB`**;
> the `0x21` path here is an **alternative to try** if `0x22` is rejected. Verify against a
> raw capture before trusting either the service or the offsets.

### 8.3 Sniffer commands for verifying all of the above

Implemented in `projects/sniffer/src/main.cpp`:

| Command | Mode | What it does |
|---------|------|--------------|
| `WATCH [id …]` | listen-only | Decode broadcast frames live (default `0x608` + `0x218`), rate-limited. Zero-risk confirmation of §9.1. |
| `ADIFF` | listen-only | Whole-bus DIFF: hold everything still ~3 s to learn each ID's noisy bytes, then toggle one input (4WD dial, diff lock, handbrake). The ID/byte that changes is printed — finds an **unknown** discrete-state frame without knowing its ID first. The path that found `0x218` gear, generalised to the whole bus. |
| `FUELLOG` | active | CSV row/cycle: `0x608` fuel + `0x218` gear vs polled rpm / accel / speed. Look for `accel≈0 & rpm>1100 & speed>0` → fuel should collapse to ~0 (the overrun test). |
| `RDLI <req> <lid>` | active | KWP `0x21` read (§9.2), e.g. `RDLI 7E1 02` (AT) / `RDLI 7E1 03` (odometer). Prints raw bytes + bcomp11's candidate decode. |
| `VERIFY [m01\|m22]` | active | Polls **every `verified=false` PID** in `pid_map.h` (Mode 01 and the Mode 22 advanced table) and reports which ones this vehicle actually answers — the shortlist for flipping `verified` to `true`. |

## 9. KWP service `0x21` candidate parameters (4M41)

UDS service `0x22` is rejected on this vehicle (NRC `0x11`/`0x80`, §8 note). The
manufacturer data is reachable instead over the older KWP2000 service `0x21`
ReadDataByLocalIdentifier: request = `21 LID`, positive response = `61 LID D0 D1 …`.

These are **candidates, not yet confirmed on the car.** The header/LID and the
decoded meaning are known; the byte offset and scale for each are not. Resolve the
exact decode with `RDLI`, then promote per the rule in §7.

### 9.1 Addressing

- 11-bit ISO-TP, protocol `6` (ELM `ATSP6`, ISO 15765-4 CAN 11-bit/500k).
- Response id is per-ECU, **not** a fixed offset. `+8` holds only inside the
  `0x7Ex` range (`7E0→7E8`, `7E1→7E9`); the other ECUs pair at `+1` (`786→787`,
  `79E→7A1`, `78C→78D`, `790→791`). Read each response id off the car to be sure.
- `21AD` is service `0x21`, LID `0xAD` (two bytes sent after `ATSH<header>`).
- Vehicle: 70 L tank, turbo diesel, INVECS-II 4AT gearbox.
- One body ECU is **K-line** (address `0x98`, KWP init `81 98 F1`), not on CAN.

### 9.2 Blocks and LIDs

| Header → resp | ECU | LIDs (`0x21 XX`) |
|-----|-----|-----|
| `7E0` → `7E8` | Engine (4M41) | `01` (rpm+speed), `02` (coolant, intake-air, gear byte), `15` (odometer, approx), `1A` (absolute load), `44` (rpm alt) |
| `7E1` → `7E9` | TCM (INVECS-II 4AT) | `01`, `02` (ATF + shaft speeds, §8.2), `03` (trans block, §9.7), `04` |
| `786` → `787` | Super Select 4x4 | `21` (traction mode 2H/4H/4HLc/4LLc, byte D0) |
| `79E` → `7A1` | Cluster | `02`, `A2`, `A3`, `A5`, `A6`, `A8`, `AA`, `AB`, `AD` (odometer) |
| `78C` → `78D` | body | `01`, `02`, `03`, `0D`, `14` |
| `7A0` → `7A1` | aux | `01`, `02`, `25` |
| `790`/`792`/`794` → `+1` | ABS / ASC | to sweep |
| `741` → `74C`, `784` → `785` | unidentified | to sweep |

### 9.3 Known decode locations

Byte letter = response data index, `A` = D0 (first byte after the `61 LID` echo):

| Target | Request | Byte | Notes |
|-----|-----|-----|-----|
| 4WD traction mode | `786 21 21` | A (D0) | 2H / 4H / 4HLc / 4LLc |
| Absolute engine load | `7E0 21 1A` | — | block 211A |
| Raw gear code | `7E0 21 02` | J (D9) | block 2102 |
| Coolant + intake-air temp | `7E0 21 02` | — | block 2102 |
| Engine rpm + speed | `7E0 21 01` | — | block 2101 |
| Engine rpm (alt) | `7E0 21 44` | — | block 2144 |

### 9.4 Signals carried by these blocks

- **TCM (`7E1`)** — ATF temperature (torque-converter and pan oil, °C), turbine
  (input) rpm, output-shaft rpm, engine rpm, converter slip / lock-up, selector
  position, manual mode, low range. ATF temperature has no broadcast-bus source.
- **Cluster (`79E`)** — odometer (LID `AD`, official dash km), fuel level in
  litres, parking brake, ECU voltage.
- **4x4 (`786`)** — traction mode and off-road / reduced-range flags.
- **Engine (`7E0`)** — coolant, intake-air temp, absolute load, alternate rpm,
  and a diesel thermal set to probe: EGT 1/2/3, DPF temperature, radiator-fan
  relay, injector opening time, fuel trims.

### 9.5 A/C compressor broadcast frame: CAN `0x16F`

The A/C compressor-clutch state is a free-running frame on CAN `0x16F`. It is not
in the 14-ID passive capture (`references/bus-inventory.md`); `WATCH 16F` with the
compressor cycling will confirm whether the OBD port exposes it.

### 9.6 Confirming on the car

1. `RDLI 7E1 02` → expect `61 02 …` on `7E9`: ATF temp + shaft speeds.
2. `RDLI 786 21` → expect `61 21 …` on `787`: byte D0 = 4WD mode.
3. `RDLI 79E AD` → dash odometer; `RDLI 79E A8`/`A2` + `RDLI 7A0 25` → fuel level + aux.
4. `RDLI 7E1 03` → second ATF candidate (§9.7). If it answers, log byte 9 warm.
5. Correlate each with `SIG` while driving before trusting a byte offset, then promote.

## 9.7 Second ATF-temp candidate — service `0x21` LID `03` (cross-model lead)

Independent of the §8.2 decode (`7E1 21 02`, ATF = `D6 − 40`). The OBDScanner project
(parhipov/OBDScanner, `core/data/cars/mitsubishi.json`) decodes Mitsubishi transmission
data over a **different LID** on the same TCM:

| Target | Request | Byte | Formula | Source confidence |
|-----|-----|-----|-----|-----|
| ATF temperature | `7E1 21 03` | D9 (`resp[11]`) | `D9 − 55` °C | tagged 6B31-only, `verified=false` |

> This formula was captured on the Outlander 3.0 **6B31** 6-speed auto, not the 4M41 /
> INVECS-II 4AT — treat the `−55` offset and the byte index as unconfirmed for this truck.
> LID `03` itself is a TCM block we have **not** swept; the value may sit at a different
> offset here. Two independent LIDs now point at ATF (`02` D6−40 and `03` D9−55) — probe
> both with `RDLI`, take whichever answers and tracks a warming gearbox, then promote.

> OBDScanner also confirms the protocol direction: every Mitsubishi car it supports uses
> service `0x21` with 1-byte LIDs (`01` / `03` / `06` / `10`) on 11-bit CAN, never UDS
> `0x22` — matching the §8 rejection of `0x22` on this vehicle.

## 9.8 Authoritative source — MUT-III SE CommonDB

The definitive per-VIN PID/DID map for the 4M41 lives inside Mitsubishi's own **MUT-III SE**
diagnostic software, in its `CommonDB` XML (`Vehicle_DB.xml`, `VIN_Selection.xml`, etc.),
which carry the real `CANBUS_ID` and data-identifier definitions per `VHCL_ID` / `ENGINE_ID`
/ `TRANSMISSION_ID`. The DB files ship as encrypted `.exdf`; the cipher is trivial
(mohammad-aliB/MUT-III-SE-Reverse-Engineering, `decrypt_exdf.py`):

```
for each byte: reverse its bit order (bit0<->bit7, ...), then XOR with 0xAA
```

If a MUT-III SE install (or just its data directory) can be obtained, decrypting
`CommonDB/*.exdf` yields Mitsubishi's own byte offsets and formulas for this exact
vehicle — replacing every `verified=false` guess in this document with ground truth.
