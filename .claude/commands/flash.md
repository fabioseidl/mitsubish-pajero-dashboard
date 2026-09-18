---
description: Build and flash one sub-project to connected hardware, then optionally open the serial monitor
argument-hint: [project name, e.g. server | server_emulator | main_hud]
allowed-tools: Bash(cd:*), Bash(pio run:*), Bash(pio device:*), Bash(~/.platformio/penv/bin/pio:*)
---

Flash `projects/$1` (ask which project if `$1` is empty):

```bash
cd projects/$1 && pio run --target upload
```

Run the host tests first (`/test`) if this flash includes server or `lib/core`
changes — the tests are the only check that runs without the vehicle.

Before flashing, confirm with me if any of these apply:

- The `Payload` struct or `PAYLOAD_VERSION` changed — **every** client must be
  reflashed too, or it will misread the layout silently.
- The board is `main_display` and the UART DIP switch is on UART2 — flashing needs
  it on USB.

Afterwards, offer the serial monitor (`pio device monitor -b 115200`). Note that
`main_hud` cannot show panic backtraces over USB at all, and `main_display` loses
its console when the switch is flipped to the GPS — see the board reference files
in the `client-firmware` skill.
