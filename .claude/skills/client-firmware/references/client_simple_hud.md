# `client_simple_hud` — CYD (ESP32 "Cheap Yellow Display")

The original client: a 2.4" 320×240 SPI dashboard driven by a SquareLine Studio
LVGL UI. The only client with an LDR, and so the only user of
`BrightnessController`'s 4-level auto-brightness.

## Board

| Item | Detail |
|---|---|
| MCU | ESP32 (`esp32doit-devkit-v1`), `espressif32@6.9.0`, Arduino |
| Panel | ILI9341, 2.4", 320×240, SPI |
| Touch | XPT2046 resistive on a **separate SPI bus**; some units carry a CST816S capacitive controller instead |
| Backlight | GPIO 27, LEDC PWM (`CYDDisplay`) |
| Light sensor | LDR on GPIO 34 (ADC, input-only) |
| Libraries | `lvgl ^9.5.0`, `LovyanGFX ^1.2.0` |
| Partitions | `huge_app.csv` |

## Pins (`include/pin_config.h`)

| Signal | GPIO |
|---|---|
| LCD MOSI / CLK / CS / DC / MISO | 13 / 14 / 15 / 2 / 12 (RST tied, `-1`) |
| Touch CS / IRQ / SCLK / MOSI / MISO | 33 / 36 / 25 / 32 / 39 |
| CST816S touch INT | 21 — **input only, never drive it as an output** |
| Backlight / LDR | 27 / 34 |
| BOOT button (active LOW, pull-up) | 0 |

GPIO 27 is the only backlight control; there is no secondary enable.

## UI

`ui/` is a **SquareLine Studio LVGL 9 export** (`ui_boot`, `ui_hud` screens, fonts
and images) pulled in by `build_src_filter`. Generated — never hand-edit; changes
belong in `CYDScreenController`. Source assets live in `ui/client_simple_hud/` at
the repo root (`.spj` project, TTFs, PNGs).

## `CYDScreenController`

- Holds a `BrightnessController&` (4 levels: 25/50/75/100, LDR-driven, with a 10 s
  manual-override hold-off after a touch or button press).
- Both the touchscreen and the physical BOOT button (GPIO 0) cycle brightness.
- The pending payload is kept as a **heap pointer**, not by value: the controller
  is a global static in `main.cpp`, and inlining the `Payload` struct (149 bytes
  as of PAYLOAD_VERSION 5, 233 before it) inflates `.dram0.bss` on a plain ESP32. It is written under a `portMUX_TYPE` spinlock in
  the ESP-NOW callback and applied to LVGL only in `tick()`.
- `freertos/FreeRTOS.h` is included **before** anything pulls in `portmacro.h`, so
  `portUSING_MPU_WRAPPERS` is not defined twice (FreeRTOS.h sets it to 0 first,
  portmacro.h to 1 later). Keep that include order.
- The header guards its LVGL and FreeRTOS includes behind `#ifndef UNIT_TEST` so
  the class still compiles in host tests.

## Init order

The backlight must be attached after the panel is up — the same trap as
`main_hud`. Follow the existing order in `main.cpp`.

## Local settings note

`projects/client_simple_hud/.claude/settings.json` used to grant
`additionalDirectories` for sessions started *inside* this folder, including a
`button_scan` project that no longer exists. Start Claude from the repo root
instead and the root `.claude/settings.json` applies.
