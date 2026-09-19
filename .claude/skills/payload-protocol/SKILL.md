---
name: payload-protocol
description: The wire contract between server and clients — the packed Payload struct, its static_assert size, PAYLOAD_VERSION, the hand-maintained OBD-II PID map, and the ESP-NOW broadcast rules. Use when adding or changing a Payload field, adding a PID or CAN broadcast frame, touching lib/core/include/payload.h or pid_map.h, or debugging clients that show zeros/garbage after a server change.
---

# Payload & PID protocol

`lib/core/include/payload.h` and `pid_map.h` are the contract every node compiles
against. Server, emulator and all four clients share these two headers — changing
either changes every binary in the repo.

## The Payload struct

- Packed C struct (`__attribute__((packed))`), currently **233 bytes**, enforced by
  `static_assert(sizeof(Payload) == 233, ...)` at the bottom of `payload.h`.
- `PAYLOAD_VERSION` is currently **4**. Clients do not currently reject a version
  mismatch, so a stale client silently misreads a changed layout.
- Broadcast at **10 Hz** (100 ms) to `FF:FF:FF:FF:FF:FF`, encrypted with the PMK.
  ESP-NOW's hard ceiling is 250 bytes — 233 leaves little headroom.

### Changing the struct — the whole checklist

Adding or removing a field means all of this, in order:

1. Edit the struct in `lib/core/include/payload.h`.
2. **Update the `static_assert` byte count** to the new size, or nothing compiles.
   Compute it by hand from the field widths (packed = no padding).
3. Bump `PAYLOAD_VERSION`.
4. Populate the field in `projects/server/src/payload_builder.cpp`.
5. Populate it in `projects/server_emulator/src/simulation_data_generator.cpp` —
   the emulator must stay byte-identical to the real server or bench-testing lies.
6. Update `test/host/test_server/test_payload_builder.cpp`.
7. **Reflash every client.** A client running the old layout reads every field
   after the insertion point at the wrong offset.

### Field type rules

- **Fixed-width types only** (`uint8_t`, `uint16_t`, `uint32_t`, `float`, `bool`).
  Never `int`, `long`, `size_t`, or an enum — host tests build for arm64 while the
  firmware is 32-bit Xtensa, and those types differ in size between the two.
- `float` (IEEE-754 single) and `bool` (1 byte) are identical on both, so they are safe.

## The PID map

`lib/core/include/pid_map.h` is **hand-maintained and never generated**. It holds:

- `PidDefinition` entries in `PID_MAP[]` — Mode 01 PIDs with explicit formula
  parameters: `value = (A * a_mult + B * b_mult) * scale + offset`, clamped to
  `[min_value, max_value]`. `b_mult = 0.0f` for single-byte PIDs.
- `FormulaType`: `FORMULA_LINEAR`, `FORMULA_BITMASK` (PID 0x01 — extracted by
  `PIDTranslator::extractMilStatus()` / `extractDtcCount()`, never by `translate()`),
  `FORMULA_COMPLEX` (special-cased in `PIDTranslator`).
- `verified` flag — `true` means confirmed answering on this vehicle. A `false`
  PID reads 0.0 when the ECU rejects it; never make a payload field depend on one.
- **Slot IDs 0xA0–0xBF** for values that have no Mode 01 PID. `DataAggregator`
  indexes a flat `float[256]` by PID, so 16-bit Mode 22 DIDs and passive CAN
  frames are remapped into this otherwise-unused range (e.g. `PID_BCAST_FUEL_RAW`
  = 0xBC, `PID_M22_AT_GEAR_POS` = 0xA0).
- `MODE22_ADVANCED_PIDS[]` — (ECU, DID) → slot mappings for UDS Mode 22.
  Dormant on this vehicle; see the `can-reverse-engineering` skill for why.

### Adding a PID

1. Add the `#define PID_*` with its hex code and a comment giving the formula.
2. Add the `PID_MAP[]` row with real `a_mult/b_mult/scale/offset/min/max` values —
   do not guess; work them out from the OBD-II formula and write them explicitly.
3. Mark `verified` honestly (`false` until the car answers it).
4. Add it to `FAST_PIDS` or `SLOW_PIDS` in `projects/server/src/main.cpp`, or it is
   never requested. Read the poll-scheduling notes in the `server-firmware` skill
   before touching `FAST_PIDS` — the list length sets the refresh rate of every
   live dashboard value.
5. Only then add a `Payload` field for it, following the checklist above.

Never hardcode a raw PID number in server or client code — always use the
`PID_*` define.

## ESP-NOW rules

- **Unidirectional.** Server broadcasts; clients receive. There are no ACKs, no
  reverse messages, no per-client state on the server. Do not add a reverse
  channel without changing this design deliberately.
- Security is a **PMK only** (16 bytes, compile-time, identical on every node).
  It lives in `lib/core/include/security_config.h`, which is **gitignored** —
  create it from `security_config.h.example`. A PMK mismatch is silent: the client
  simply never receives anything.
- All nodes must sit on the **same WiFi channel** (channel 1).
- `ESPNowBroadcaster` / `ESPNowReceiver` in `lib/core` are the only ESP-NOW
  implementations — never reimplement the transport in a sub-project.
- The receive callback runs in the WiFi task. Clients must copy the payload and
  apply it in their own `tick()`; LVGL is not thread-safe.
