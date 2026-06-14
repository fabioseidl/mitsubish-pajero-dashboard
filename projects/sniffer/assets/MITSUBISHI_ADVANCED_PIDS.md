# Mitsubishi Advanced OBD-II PIDs — Reverse-Engineering Reference

> Source: reverse-engineering of the Android app **"Advanced LT for MITSUBISHI 2.0"**
> (`com.ideeo.miadvancedlite`), a Torque Pro plugin.
> Purpose of this document: provide everything needed to read these manufacturer-specific
> ("advanced") parameters **directly from an ESP32** (or any UDS-capable device), without
> Torque Pro and without the original app.

---

## 1. Background — how the original app works

The APK is **not** a diagnostic tool. It never opens the CAN bus itself. It is a *Torque Pro plugin*
that does only one thing:

1. Torque broadcasts the intent `org.prowl.torque.PID_QUERY`.
2. The plugin (`PluginReceiver` → `PluginService`) binds to the Torque service
   `org.prowl.torque.remote.ITorqueService` and calls **AIDL transaction 42** (`sendPIDData`),
   handing Torque a list of *custom PID definitions* (name, header, mode+PID, equation, unit, min, max).
3. Torque then drives the ELM327 adapter, sends the OBD/UDS requests, parses the bytes, and
   evaluates the equations.

So the "advanced data" is simply a set of **manufacturer-specific UDS PIDs** (UDS Service `0x22`,
*ReadDataByIdentifier*) sent to specific ECU CAN addresses. To reproduce it on an ESP32 we send the
exact same UDS requests on the CAN bus and apply the same equations.

### Obfuscation note (for traceability)
In the APK the PID tables are lightly obfuscated with a **self-inverse ROT13 (letters) + ROT5 (digits)**
cipher (`com.ideeo.miadvancedlite.c.a(String)`). Equations, units and short names are stored encoded;
the DID is stored in plaintext in the last column. **All values in this document are already decoded.**
The cipher is included in the appendix for verification.

Key fact confirmed from the decompiled data: the `startDiagnostic` / `stopDiagnostic` columns are
**empty** for every PID. → **No diagnostic session (`10 xx`) and no TesterPresent (`3E 00`) are required.**
You send the `22 xx xx` request directly.

---

## 2. Bus & protocol parameters

All listed models (2008+) use **ISO 15765-4 CAN, 11-bit IDs, 500 kbit/s**.
(The app also carries legacy ISO-9141 / ISO-14230 KWP and 29-bit CAN maps for older ECUs, but every
model in the PID tables below uses 11-bit CAN.)

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

### Step 5 — Map data bytes to Torque letters and apply the equation
Torque's equation letters map to the data payload **after** the 2-byte DID echo:

| Torque letter | A | B | C | D | E | F | … | L |
|---------------|---|---|---|---|---|---|---|---|
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

### 4M41 — Pajero IV 3.2 DI-D
| Parameter | ECU | Req | Bytes | Formula | Unit | Range |
|-----------|-----|-----|-------|---------|------|-------|
| Fuel Temperature | 0x7E0 | `22 20 F2` | D4 | `D4 − 40` | °C | −40–145 |
| Cooling Fan Duty | 0x7E0 | `22 21 51` | D1 | `D1` | % | 0–100 |
| Input Speed | 0x7E1 | `22 20 AB` | D1,D2 | `(D1×256 + D2) × 0.5` | rpm | 0–7000 |
| Output Speed | 0x7E1 | `22 20 AB` | D3,D4 | `(D3×256 + D4) × 0.5` | rpm | 0–7000 |

> ⚠ **Clutch 2 byte offset is an inference.** In this *Lite* build, Clutch 1 and Clutch 2 carry an
> identical decoded formula (the byte-offset selector lives in an obfuscated Torque-internal field that
> isn't recoverable here). Clutch 1 = bytes D0,D1 is certain; Clutch 2 almost certainly uses the next
> pair (D2,D3) of the same `0x214F` response. **Verify with a raw capture before trusting Clutch 2.**

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
5. Engine must be **running** for most values (the app explicitly warns about this).
6. These are **read-only** UDS reads. Do **not** send writes to the TCM.

---

## 8. Appendix — the de-obfuscation cipher

`com.ideeo.miadvancedlite.c.a(String)` — self-inverse, char-by-char:

| Input range | Transform |
|-------------|-----------|
| `a`–`m` | `+13` |
| `n`–`z` | `−13` |
| `A`–`M` | `+13` |
| `N`–`Z` | `−13` |
| `0`–`4` | `+5` |
| `5`–`9` | `−5` |
| other | unchanged |

Python:
```python
def ca(s):
    out=[]
    for ch in s:
        o=ord(ch)
        if   97<=o<=109: o+=13
        elif 110<=o<=122: o-=13
        elif 65<=o<=77:  o+=13
        elif 78<=o<=90:  o-=13
        elif 48<=o<=52:  o+=5
        elif 53<=o<=57:  o-=5
        out.append(chr(o))
    return ''.join(out)
```

### Raw → decoded examples (from the APK arrays)
| Field (raw, obfuscated) | Decoded |
|-------------------------|---------|
| equation `((O*701)+P)*5.05` | `((B*256)+C)*0.50` |
| equation `(R-95)` | `(E-40)` |
| equation `FVTARQ((N*701)+O)*5.0` | `SIGNED((A*256)+B)*0.5` |
| equation `(N & 673) > 2` | `(A & 128) > 7` |
| unit `ecz` | `rpm` |
| unit `°P` | `°C` |
| DID column `-20f2,...` | DID is plaintext → `0x20F2` |

### Source-file → model map (APK `com/ideeo/miadvancedlite/`)
`d.java`=ASX/RVR 2.0 CVT · `e.java`=ASX 2.2 AT · `f.java`=Colt · `g.java`=L200 IV ·
`h.java`=Lancer 1.5/1.6 · `i.java`=Lancer 2.0/2.4 CVT · `j.java`=Lancer SST ·
`k.java`=Outlander 2.0/2.4 CVT · `l.java`=Outlander 2.2 DI-D · `m.java`=Outlander 2.3 DI-D ·
`n.java`=Pajero IV 3.2 DI-D. PID dispatch table & protocol maps live in `c.java`; Torque AIDL in
`d/a/a/a/a.java`.

---

## 9. Second source — igkov `bcomp11` (passive broadcast channels + legacy KWP)

> Source: <https://github.com/igkov/bcomp11> — an open-source Pajero trip computer
> (`obd.c` request/ISO-TP, `bcomp.c` decoding). This source is **independent of the
> Torque plugin above** and contributes two things that table doesn't have: data
> read **passively from free-running broadcast frames** (no request at all), and a
> **legacy KWP service `0x21`** path for the transmission/odometer.

### 9.1 Free-running broadcast frames (no request needed) — **confirmed on this truck**

Both IDs below are already present in `dumps/candump.log` on our Pajero IV (0x608 at
~5–10 Hz, 0x218 at ~50 Hz). Because they are broadcast continuously, the sniffer reads
them in **listen-only** mode — zero bus risk.

| CAN ID | Meaning (bcomp11) | Bytes | Formula | Notes |
|--------|-------------------|-------|---------|-------|
| `0x608` | Diesel injected fuel quantity | D5,D6 | `D5×256 + D6` (raw) | bcomp11 `bcomp.raw_fuel`; D0 also tracks load. If this is real injected fuel it **drops to ~0 on overrun** — unlike the MAF estimate. |
| `0x218` | Transmission status / gear | D2 | `D2 & 0x0F` | bcomp11 `data[2] & 0x0F` = selected gear / PRNDL. |

> ⚠ Byte offsets are **bcomp11's** and **unverified on this exact vehicle**. The sniffer
> prints the raw bytes alongside the decode so they can be confirmed on a real drive.
> If `0x608` confirms, it can **replace the MAF→AFR estimate** in
> `projects/server/src/derived_calculator.cpp` with a real injected-fuel reading.

### 9.2 Legacy KWP `0x21` ReadDataByLocalIdentifier (TCM)

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

### 9.3 Sniffer commands for verifying all of the above

Implemented in `projects/sniffer/src/main.cpp`:

| Command | Mode | What it does |
|---------|------|--------------|
| `WATCH [id …]` | listen-only | Decode broadcast frames live (default `0x608` + `0x218`), rate-limited. Zero-risk confirmation of §9.1. |
| `FUELLOG` | active | CSV row/cycle: `0x608` fuel + `0x218` gear vs polled rpm / accel / speed. Look for `accel≈0 & rpm>1100 & speed>0` → fuel should collapse to ~0 (the overrun test). |
| `RDLI <req> <lid>` | active | KWP `0x21` read (§9.2), e.g. `RDLI 7E1 02` (AT) / `RDLI 7E1 03` (odometer). Prints raw bytes + bcomp11's candidate decode. |
| `VERIFY [m01\|m22]` | active | Polls **every `verified=false` PID** in `pid_map.h` (Mode 01 and the Mode 22 advanced table) and reports which ones this vehicle actually answers — the shortlist for flipping `verified` to `true`. |
