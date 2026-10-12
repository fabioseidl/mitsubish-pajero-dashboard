# diagnostic

A standalone OBD-II fault scanner. Plug it into the OBD-II port, connect a phone
to its access point, and it reports whether the truck has anything wrong with it.

No laptop, no dashboard, no ESP-NOW — this node talks to nothing but the engine
ECU and the browser in your hand.

## Using it

1. Ignition on (engine running or not).
2. Plug the adapter into the OBD-II port.
3. Join the Wi-Fi network **`pajero-diag`**, password `pajero4m41`.
4. Open <http://192.168.4.1> and press **Scan**.

The page shows each step as it runs, then a verdict: whether the check-engine
lamp is commanded on, every trouble code found, and how far the readiness
monitors have got.

Change the SSID and password in `include/diag_config.h`.

## What it reads

Standard OBD-II (SAE J1979 over ISO 15765-4) against the engine ECU at
`0x7E0`/`0x7E8`:

| Mode | What it gets |
|---|---|
| `01` PID 00 | proof the ECU is awake |
| `01` PID 01 | MIL state, confirmed DTC count, readiness monitors |
| `03` | stored codes — confirmed, lamp-illuminating |
| `07` | pending codes — seen once, not yet confirmed |
| `0A` | permanent codes — survive a battery disconnect |
| `02` PID 02 | which code captured the freeze frame |
| `04` | clear codes, only on explicit confirmation |

A code found in more than one list is shown once, labelled with the most serious
status it holds.

Generic SAE codes get a description from the table in `lib/core/include/dtc.h`,
which leans toward what a 4M41 diesel actually sets. Manufacturer-specific codes
(`P1xxx`, and maker-extended `P0xxx`) are still **reported** — the number comes
from the ECU — they just carry no description.

## What it cannot read

**Only the engine ECU answers diagnostics over CAN on this vehicle.** ABS, SRS,
4WD/SS4-II and ETACS are K-line (MUT-II protocol) and are unreachable from the
OBD-II CAN pins. This is a property of the truck, not a limitation of the code:
see `.claude/skills/can-reverse-engineering/references/mut3-database.md`, which
settles it from Mitsubishi's own MUT-III CAN database.

So a clean scan here means *the engine ECU reports no faults*. It does not mean
the airbag light is off.

Mode 22 is deliberately absent. These ECUs answer NRC `0x11`
(serviceNotSupported) to every DID — already established and recorded in the
`can-reverse-engineering` skill.

## Clearing codes

The **Clear codes** button sends Mode 04, and asks first. It erases stored
codes, freeze-frame data **and the readiness monitors**, which then need a full
drive cycle to complete again. If the underlying fault is still there, the code
returns. Clearing before an emissions test is counterproductive for exactly this
reason.

The HTTP endpoint will not act on a bare request: it requires an explicit
confirmation parameter, so a stray `POST /api/clear` cannot wipe anything.

## Build and flash

```bash
cd projects/diagnostic && pio run
cd projects/diagnostic && pio run --target upload
```

Same hardware as `projects/server` and `projects/sniffer`: ESP32-S3 with an
MCP2515 on CAN A. Pins are in `include/pin_config.h`.

The MCP2515 runs in **normal mode**, not listen-only — it has to transmit to ask
the ECU anything, so this node ACKs bus traffic like any other participant. The
sniffer's passive modes remain the zero-risk option for just watching.

## Layout

```
include/pin_config.h   SPI + MCP2515 pins
include/diag_config.h  AP credentials, limits
include/web_page.h     the page, embedded in flash as a PROGMEM string
src/main.cpp           CAN, ISO-TP, the scan step machine, HTTP
```

Decoding lives in `lib/core/include/dtc.h` rather than here, so it is shared and
host-testable — `test/host/test_lib/test_dtc.cpp` covers the code-word split,
response parsing with and without the J1979 count byte, zero padding, and the
inverted readiness bits. Run it with `cd test && pio test -e native_tests`.

The scan runs one step per `loop()` pass rather than blocking, so the page stays
responsive while a request is in flight. The page polls `/api/state`; a
synchronous `WebServer` and SSE do not mix well, and a 400 ms poll is ample for
steps that take ~100 ms.
