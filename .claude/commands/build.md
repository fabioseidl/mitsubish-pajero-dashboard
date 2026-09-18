---
description: Build one sub-project with PlatformIO
argument-hint: [project name, e.g. server | server_emulator | main_display | main_hud | glass_display | client_simple_hud]
allowed-tools: Bash(cd:*), Bash(pio run:*), Bash(~/.platformio/penv/bin/pio run:*), Bash(ls:*)
---

Build `projects/$1` (ask which project if `$1` is empty):

```bash
cd projects/$1 && pio run
```

If `pio` is not on PATH, use `~/.platformio/penv/bin/pio`.

Before reporting a failure, check the usual causes:

- `lib/core/include/security_config.h` missing — it is gitignored; create it from
  `security_config.h.example` and set the same PMK as every other node.
- A `Payload` change that did not update the `static_assert` byte count in
  `lib/core/include/payload.h`.
- A library version bump. Several projects pin versions deliberately
  (LovyanGFX 1.1.x on `main_display`, Arduino_GFX ~1.5.0 on `main_hud`) — check
  the board's reference file in the `client-firmware` skill before changing one.

Report the real compiler error, not a summary of it.
