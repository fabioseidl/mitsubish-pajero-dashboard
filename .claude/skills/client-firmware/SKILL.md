---
name: client-firmware
description: Building and debugging the ESP-NOW display clients (main_display, main_hud, glass_display, client_simple_hud) — the shared IScreenController/ESPNowReceiver contract, LVGL threading rules, brightness handling, and per-board hardware quirks. Use when working in any projects/ client, adding a widget or screen, wiring a new display board, or debugging a blank/dark panel, touch, or a client that shows no data.
---

# Display clients

Every client is a **pure renderer**. It receives a `Payload` over ESP-NOW and draws
it. No CAN, no session maths, no derivation, no messages back to the server — if a
value is missing, add it on the server (see the `server-firmware` skill).

| Project | Board | Panel | Details |
|---|---|---|---|
| `main_display` | Waveshare ESP32-S3-Touch-LCD-7B | 7" 1024×600 RGB parallel | `references/main_display.md` |
| `main_hud` | Guition JC3248W535 (ESP32-S3) | 3.5" 320×480 QSPI, AXS15231B | `references/main_hud.md` |
| `glass_display` | Waveshare ESP32-C6-LCD-1.47 | 1.47" 172×320 SPI, ST7789 | `references/glass_display.md` |
| `client_simple_hud` | CYD (ESP32 Cheap Yellow Display) | 2.4" 320×240 SPI, ILI9341 | `references/client_simple_hud.md` |

Read the matching reference file before touching a board — each one documents
hardware quirks that are expensive to rediscover.

## The shared contract (`lib/core`)

```
ESP-NOW ISR → ESPNowReceiver → ServerConnectionMonitor + IScreenController → LVGL widgets
```

- **`IScreenController`** — `begin()`, `onPayloadReceived(const Payload&)`,
  `onServerStatusChanged(bool online)`, `tick()`. Every client implements it.
- **`ESPNowReceiver`** — `begin(PMK_KEY)` + `setCallback()`. Never reimplement ESP-NOW.
- **`ServerConnectionMonitor`** — feed it `onPayloadReceived(now_ms)` and call
  `tick(now_ms)` from the loop; it fires the online/offline callback after
  `DEFAULT_TIMEOUT_MS` (2000 ms) of silence.
- **`IDisplay`** — `begin()` + `setBacklightPercent(uint8_t)`. The indirection that
  lets very different backlight hardware share one brightness stepper.
- **`StepBrightness`** — 10 levels (10…100%), starts at 50%. `next()` cycles and
  wraps for a single button (`main_display`); `increase()`/`decrease()` clamp at
  the ends for a +/- pair (`main_hud`).
- **`BrightnessController`** — 4 levels (25/50/75/100) plus LDR auto-brightness with
  a 10 s manual-override hold-off. CYD only; the other boards have no light sensor.

Both brightness classes exist on purpose — do not "unify" them. They differ in
level count and in whether a light sensor drives them.

## LVGL threading — the rule that breaks clients

**LVGL is not thread-safe and all of it must run on one task.** The ESP-NOW receive
callback runs on the WiFi task, so a client must never touch a widget from there.
The pattern in every client: copy the payload into a pending slot in the callback,
set a flag, and apply it in `tick()` on the LVGL task.

Corollaries that keep showing up:
- Repaint only when a value actually changed. Clients compare against a "currently
  shown" copy before setting label text; blind redraws cost flush bandwidth and,
  on single-framebuffer boards, cause visible tearing.
- Advance LVGL time with `lv_tick_inc()` / a tick callback and run
  `lv_timer_handler()` from the same loop.

## Conventions

- Pin assignments go in the project's `include/pin_config.h` as named constants —
  never a raw GPIO number inline.
- `lib/core/include/security_config.h` must exist and carry the **same PMK as the
  server**, or the client receives nothing, silently. It is gitignored; create it
  from `security_config.h.example`.
- All nodes stay on WiFi **channel 1**.
- Generated files — SquareLine Studio exports (`ui/`, `src/ui/`) and
  `lv_font_conv` font tables (`ui_font_*.c`) — are **never hand-edited**. They are
  overwritten on the next export. Put adaptation code in a bridge module
  (`app_ui.cpp` in `main_display` is the pattern).
- LVGL is v9 throughout. The v8 APIs (`lv_disp_drv_t`, `lv_indev_drv_t`) are gone;
  use `lv_display_create()` / `lv_indev_create()`.

## Debugging a client that shows nothing

Work down this list — it is almost always one of these:

1. **PMK mismatch** between client and server `security_config.h` → silent, no data.
2. **Wrong WiFi channel.**
3. **Stale `Payload` layout** — the client was not reflashed after a struct change.
   Every field after the insertion point reads garbage. Check `PAYLOAD_VERSION`.
4. **`PAYLOAD_FLAG_DATA_VALID` clear** — the server is not seeing RPM/speed.
5. **Panel dark but powered** — a board-level init problem, not a data problem.
   Go to the board's reference file.
