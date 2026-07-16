# CLAUDE.md — projects/main_hud

Read the root `CLAUDE.md` before working on this project.

---

## Purpose

A speed-only ESP-NOW client. Receives the `Payload` broadcast by `projects/server`
(or `server_emulator`) and renders `speed_kmh` in a very large Roboto Bold face —
nothing else. Like every client it is a pure renderer: no CAN, no session maths.

Two display modes, toggled by an on-screen button:

| Mode | Use |
|---|---|
| `SIMPLE` | Panel read directly, normal orientation. |
| `HUD` | Panel reflects off the windshield; the frame is transformed on its way to the glass. |

The UI is only the speed. The two buttons hide after `BUTTONS_IDLE_HIDE_MS` (3 s)
without a touch and come back on the next one — so at speed the screen shows a
number and nothing else. The tap that brings the buttons back is deliberately
**not** delivered to them (`wake_only_` in `touchReadCb`): a wake-up tap that
happened to land on the brightness button would otherwise change brightness by
accident. First tap wakes, second acts.

The speed is centred on the whole screen and ignores the buttons — they overlay
it and are hidden most of the time, so letting them push the number sideways
would leave it off-centre in the display's normal state.

---

## Hardware

| Component | Detail |
|---|---|
| Board | Guition JC3248W535 (ESP32-S3-N16R8: 16 MB flash, 8 MB OPI PSRAM) |
| Panel | 3.5" 320x480 IPS, AXS15231B controller, **QSPI** bus |
| Touch | AXS15231B capacitive, own I2C bus, address `0x3B` |
| Backlight | GPIO 1, active-HIGH, LEDC PWM |

Pin assignments live in `include/pin_config.h` — never inline raw GPIO numbers.

**GPIO 8 is the touch I2C clock.** Several pinouts online label it "LCD DC";
that is wrong for this board, because QSPI encodes command vs. data in the
transfer itself and there is no DC line.

---

## Why Arduino_GFX and not LovyanGFX

Every other client uses LovyanGFX, but no released LovyanGFX version ships an
AXS15231B panel driver (QSPI support only recently landed on its develop branch).
Arduino_GFX has `Arduino_AXS15231B` and is the stack the community has working on
this board, so this project deviates deliberately. Everything above the driver —
`Payload`, `ESPNowReceiver`, `ServerConnectionMonitor`, `IDisplay`,
`IScreenController` — is the same shared `lib/core` as the rest of the repo.

**Arduino_GFX is held at `~1.5.0` on purpose.** 1.6.0+ includes
`esp32-hal-periman.h`, which only exists in Arduino core 3.x, while
`espressif32@6.9.0` pins core 2.0.17. Bumping it means bumping the platform for
the whole repo.

---

## Rendering path

```
LVGL (partial buffer, internal RAM)
  → flushCb  — mirrors the pixels in HUD mode
  → Arduino_Canvas framebuffer (307 KB, PSRAM)
  → canvas->flush() — full-frame QSPI push to the panel
```

The canvas is not optional. The AXS15231B mishandles partial window writes, so
every update has to arrive as a complete frame; the canvas absorbs LVGL's partial
areas and always pushes the whole thing.

Two consequences worth remembering:

- **A flush costs ~15 ms** (307 KB over QSPI at 40 MHz). `tick()` therefore only
  pushes when `flushCb` reports that LVGL actually drew something. Do not make
  the flush unconditional — it would saturate the bus at the 5 ms loop rate.
- **Mirroring happens in `flushCb`, not in the panel or the widget tree.** The
  AXS15231B has no hardware mirror and LVGL has no whole-display flip. LVGL
  always lays out un-mirrored and never knows the mode, which is exactly why
  `touchReadCb` has to mirror touch input back the other way — otherwise the
  buttons would stop matching where the user sees them.

`MIRROR_HORIZONTAL` / `MIRROR_VERTICAL` in `hud_screen_controller.h` set the flip
axes. They are independent, so all four combinations are available:

| H | V | HUD mode shows |
|---|---|---|
| ✓ | ✗ | left-right mirror |
| ✗ | ✓ | mirrored **and** upside down (**current**) |
| ✓ | ✓ | plain 180° rotation — upside down, but NOT mirrored |
| ✗ | ✗ | same as SIMPLE |

The V-only row is the non-obvious one: a top-to-bottom flip *is* a left-right
mirror plus a 180° rotation, because the two compose —
`mirror_x` then `rotate_180` gives `(x,y) → (W-1-x, y) → (x, H-1-y)`. Setting
both flags cancels the mirror back out, leaving a bare rotation.

Tune these against the real windshield; the optics depend on how the panel is
mounted, so the right combination is the one that reads correctly on the glass.

**The buttons stay in the same physical corner in both modes** (mode top-right,
brightness bottom-right, as the driver sees them). `positionButtons()` anchors
them to the *opposite* corner in LVGL's space whenever a flip is active, which
the flush transform then cancels out. Their labels do flip with everything else —
only position is compensated, not glyph orientation.

---

## Classes in This Project

| Class | File | Responsibility |
|---|---|---|
| `HudScreenController` | `include/hud_screen_controller.h` | LVGL UI, mode switching, mirroring, touch routing |
| `StepBrightness` | `lib/core/include/step_brightness.h` | 10-step cycle: 10, 20, … 100%, wrapping (shared with `main_display`) |
| `HudDisplay` | `include/hud_display.h` | `IDisplay` impl — LEDC backlight on GPIO 1 |
| `AXS15231BTouch` | `include/axs15231b_touch.h` | Touch controller I2C protocol |

`StepBrightness` lives in `lib/core` because `main_display` cycles the same ten
levels from its own backlight button. It drives an `IDisplay`, which is what lets
the two boards share it despite wildly different hardware: here `HudDisplay` is
LEDC PWM on GPIO 1, while `main_display` has no PWM at all and implements
`setBacklightPercent()` as the opacity of an LVGL overlay.

Neither reuses `lib/core`'s `BrightnessController`: that one is fixed at four
levels (25/50/75/100) and coupled to an LDR, and this board has no light sensor
and needs fine control at the dim end for night driving. `HudDisplay` likewise
stays local rather than reusing `CYDDisplay`, which is named for the CYD board
and hardcodes a 75% startup level.

---

## Init order

`Arduino_GFX` reconfigures GPIOs during `begin()`, so the LEDC backlight must be
attached *after* the panel is up. `main.cpp` calls `screen.begin()` before
`display.begin()` for exactly this reason — the same ordering trap as
`client_simple_hud`.

---

## Fonts

`src/ui_font_roboto_bold_*.c` are generated, not hand-edited. Regenerate with:

```bash
lv_font_conv --font ui/client_simple_hud/assets/Roboto-Bold.ttf --size 200 \
  -r 0x2D -r 0x30-0x39 --bpp 4 --no-compress --format lvgl --lv-include lvgl.h \
  -o projects/main_hud/src/ui_font_roboto_bold_200.c
```

The 200 px face carries digits and `-` only (that is all the speed readout and its
`--` offline placeholder ever show); the 28 px face carries full ASCII for the
button labels. Widening the 200 px range costs flash fast — it is ~430 KB as is.

---

## Build

```bash
cd projects/main_hud
pio run                 # build
pio run --target upload # flash over USB-C
```

`lib/core/include/security_config.h` must exist (see root `CLAUDE.md`) — the PMK
has to match the server's or no payloads will decrypt.

---

## Serial output — read this before debugging

This board has **no USB-UART bridge chip**; the S3's native USB-Serial-JTAG is the
only port. That inverts `main_display`'s advice:

| | `main_display` (has bridge) | `main_hud` (native USB) |
|---|---|---|
| `ARDUINO_USB_CDC_ON_BOOT` | `0` | **`1`** |
| Serial lands on | UART0 → bridge → USB | HWCDC → USB-Serial-JTAG |

The IDF console (bootloader logs **and panic backtraces**) is UART0 on GPIO43/44
regardless, and those pins go nowhere on this board. **You cannot see a crash
backtrace over USB.** Debug by print, or wire a UART adapter to GPIO43/44.

A healthy boot looks like:

```
psramInit(): PSRAM enabled
=== main_hud SETUP START ===
[DISPLAY] backlight init GPIO1 ch7
[MAIN] PSRAM: found (8386295 bytes free)
[SCREEN] display 480x320 ready
[MAIN] listening on WiFi channel 1
```

---

## Troubleshooting

**Endless `ESP-ROM:esp32s3` banner, no app output, ~25 ms per cycle.**
The bootloader is rejecting the partition table and resetting before the app runs.
Almost always a flash-size mismatch: `board_upload.flash_size` (what esptool
stamps into the image header) must be `16MB`. It is *not* set by
`board_build.flash_size`, and `esp32-s3-devkitc-1` defaults it to 8 MB — which
silently contradicts `default_16MB.csv`. Because bootloader logs are invisible
(see above), this fails with no diagnostic whatsoever.

**Blank but backlit panel.** The app got past `display.begin()` and died later —
read the `[SCREEN]` trace to see which init step was last.

**Nothing on screen at all, not even a glow.** Backlight is switched on before
anything else can fail, so this points at power or GPIO 1, not at init order.
