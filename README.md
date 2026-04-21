# car-dashboard

Distributed car dashboard built on ESP32. A server node reads vehicle data from the OBD-II port via CAN bus and broadcasts it wirelessly. Client nodes receive the data and render it on displays.

---

## Hardware

| Node | Components |
|---|---|
| Server | ESP32 + MCP2515 + TJA1050 (OBD-II CAN adapter) |
| Client (CYD) | ESP32 Cheap Yellow Display — ILI9341 2.4" 320x240, XPT2046 touch |

Target vehicle: Mitsubishi Pajero Dakar (diesel).

---

## Projects

```
projects/server/           Reads CAN bus, broadcasts Payload via ESP-NOW
projects/server_emulator/  Generates synthetic data — use for client dev without the car
projects/client_cyd/       Renders dashboard on CYD 2.4" display
```

Shared library in `lib/core/`.

---

## Setup

**1. Clone and create security config**

```bash
git clone git@github.com:fabioseidl/mitsubish-pajero-dashboard.git
cd car-dashboard
cp lib/core/include/security_config.h.example \
   lib/core/include/security_config.h
```

Edit `security_config.h` and set a 16-byte PMK. This file is gitignored — never commit it.

**2. Run host tests**

```bash
cd test && pio test -e native_tests
```

All tests must pass before flashing.

**3. Build and flash**

```bash
# Server
cd projects/server && pio run --target upload

# Client
cd projects/client_cyd && pio run --target upload
```

Use `server_emulator` instead of `server` when developing without the vehicle.

---

## Development

This project follows TDD. Write a failing test before writing any production code. See `docs/06_test_spec.md` for the full test plan and `CLAUDE.md` for Claude Code session rules.

Full documentation is in `docs/`.
