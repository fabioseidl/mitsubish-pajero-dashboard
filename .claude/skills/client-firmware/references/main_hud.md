# `main_hud` — Guition JC3248W535 (ESP32-S3)

A speed-only client: `speed_kmh` in a very large Roboto Bold face, plus a pair
of +/- brightness buttons.

Mounted **vertically**: `SCREEN_ROTATION` is 2, so LVGL works in a 320x480
portrait frame. The panel is natively portrait, so rotation 2 is a plain 180°
flip of the native frame — no axis swap.

The panel is read directly. There is no windshield-reflection mode: the frame
reaches the canvas untransformed, and `flushCb` is a straight blit.

## Board

| Item | Detail |
|---|---|
| MCU | ESP32-S3-N16R8 — 16 MB flash, 8 MB OPI PSRAM |
| Panel | 3.5" 320×480 IPS, AXS15231B controller, **QSPI** bus |
| Orientation | Portrait, `SCREEN_ROTATION` 2 — LVGL sees 320×480 |
| Touch | AXS15231B capacitive, own I²C bus, address `0x3B` |
| Backlight | GPIO 1, active-HIGH, LEDC PWM |
| PlatformIO | `espressif32@6.9.0`, `esp32-s3-devkitc-1`, Arduino, `qio_opi`, `default_16MB.csv` |

Pins live in `include/pin_config.h`. **GPIO 8 is the touch I²C clock** — several
pinouts online label it "LCD DC", which is wrong for this board: QSPI encodes
command vs. data in the transfer itself, so there is no DC line.

## Why Arduino_GFX instead of LovyanGFX

No released LovyanGFX ships an AXS15231B driver (QSPI support only landed on its
develop branch), while Arduino_GFX has `Arduino_AXS15231B` and is what the
community has working on this board. Everything above the driver — `Payload`,
`ESPNowReceiver`, `ServerConnectionMonitor`, `IDisplay`, `IScreenController` — is
the same shared `lib/core` as the rest of the repo.

**Arduino_GFX is held at `~1.5.0` on purpose.** 1.6.0+ includes
`esp32-hal-periman.h`, which exists only in Arduino core 3.x, while
`espressif32@6.9.0` pins core 2.0.17. Bumping it means bumping the platform for
the whole repo.

## Rendering path

```
LVGL (partial buffer, internal RAM)
  → flushCb — blits the area into the canvas
  → Arduino_Canvas framebuffer (307 KB, PSRAM)
  → canvas->flush() — full-frame QSPI push
```

The canvas is not optional: the AXS15231B mishandles partial window writes, so
every update must arrive as a complete frame, and the canvas absorbs LVGL's
partial areas.

- **A flush costs ~15 ms** (307 KB over QSPI at 40 MHz). `tick()` therefore pushes
  only when `flushCb` reports LVGL actually drew something. Making the flush
  unconditional saturates the bus at the 5 ms loop rate.
## UI behaviour

Brightness runs down the right edge: `+` top-right, the current percent under it,
`-` bottom-right. The two buttons sit at opposite ends of a 480 px screen so a
blind tap on a moving vehicle cannot hit the wrong one.

They drive `StepBrightness::increase()` / `decrease()`, which **clamp** at 100%
and 10% rather than wrapping — `next()`'s wrap would drop a 100% screen to 10%
on a mistaken tap. A clamped press never reaches the display.

Both buttons and the percent label hide together after `BUTTONS_IDLE_HIDE_MS`
(3 s) without a touch and come back on the next one, so at speed the screen shows
a number and nothing else.

The tap that brings them back is deliberately **not** delivered to them
(`wake_only_` in `touchReadCb`): a wake-up tap landing on a brightness button
would otherwise change brightness by accident. First tap wakes, second acts.

The speed is centred on the whole screen and ignores the buttons — they overlay it
and are hidden most of the time, so letting them push the number sideways would
leave it off-centre in the display's normal state.

## Classes

| Class | File | Responsibility |
|---|---|---|
| `HudScreenController` | `include/hud_screen_controller.h` | LVGL UI, brightness buttons, touch routing |
| `HudDisplay` | `include/hud_display.h` | `IDisplay` — LEDC backlight on GPIO 1 |
| `AXS15231BTouch` | `include/axs15231b_touch.h` | Touch controller I²C protocol |
| `StepBrightness` | `lib/core/include/step_brightness.h` | Shared 10 levels; `increase()`/`decrease()` clamp |

`HudDisplay` stays local rather than reusing `CYDDisplay`, which is named for the
CYD board and hardcodes a 75% startup level. `BrightnessController` is not reused
either: it is fixed at four levels and coupled to an LDR, and this board has no
light sensor and needs fine control at the dim end for night driving.

## Init order

`Arduino_GFX` reconfigures GPIOs during `begin()`, so the LEDC backlight must be
attached **after** the panel is up. `main.cpp` calls `screen.begin()` before
`display.begin()` for exactly this reason — the same trap as `client_simple_hud`.

## Fonts

`src/ui_font_roboto_bold_*.c` are generated, not hand-edited:

```bash
lv_font_conv --font ui/client_simple_hud/assets/Roboto-Bold.ttf --size 180 \
  -r 0x2D -r 0x30-0x39 --bpp 4 --no-compress --format lvgl --lv-include lvgl.h \
  -o projects/main_hud/src/ui_font_roboto_bold_180.c
```

The 180 px face carries digits and `-` only (all the speed readout and its `--`
offline placeholder ever show); the 28 px face carries full ASCII for the button
labels. Widening the 180 px range costs flash fast — it is ~348 KB as is.

**180 px is a ceiling set by the portrait width.** A Roboto Bold digit advances
`0.574 × size`, so three digits need `1.72 × size` px: 310 px at 180, against a
`SCREEN_W` of 320. Anything larger clips the speed at 100 km/h and above. Resize
the face if `SCREEN_ROTATION` ever goes back to a landscape value.

## Serial output — read before debugging

This board has **no USB-UART bridge chip**; the S3's native USB-Serial-JTAG is the
only port. That inverts `main_display`'s setup:

| | `main_display` (has bridge) | `main_hud` (native USB) |
|---|---|---|
| `ARDUINO_USB_CDC_ON_BOOT` | `0` | **`1`** |
| Serial lands on | UART0 → bridge → USB | HWCDC → USB-Serial-JTAG |

The IDF console (bootloader logs **and panic backtraces**) is UART0 on GPIO 43/44
regardless, and those pins go nowhere on this board. **You cannot see a crash
backtrace over USB.** Debug by print, or wire a UART adapter to GPIO 43/44.

A healthy boot looks like:

```
psramInit(): PSRAM enabled
=== main_hud SETUP START ===
[DISPLAY] backlight init GPIO1 ch7
[MAIN] PSRAM: found (8386295 bytes free)
[SCREEN] display 320x480 ready
[MAIN] listening on WiFi channel 1
```

## Troubleshooting

**Endless `ESP-ROM:esp32s3` banner, no app output, ~25 ms per cycle.** The
bootloader is rejecting the partition table and resetting before the app runs.
Almost always a flash-size mismatch: `board_upload.flash_size` (what esptool
stamps into the image header) must be `16MB`. It is *not* set by
`board_build.flash_size`, and `esp32-s3-devkitc-1` defaults it to 8 MB, silently
contradicting `default_16MB.csv`. Because bootloader logs are invisible here, this
fails with no diagnostic whatsoever.

**Blank but backlit panel.** The app got past `display.begin()` and died later —
read the `[SCREEN]` trace to see which init step was last.

**Nothing at all, not even a glow.** Backlight is switched on before anything else
can fail, so this points at power or GPIO 1, not init order.
