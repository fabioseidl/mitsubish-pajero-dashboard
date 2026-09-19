# `main_display` — Waveshare ESP32-S3-Touch-LCD-7B

The 7" main dashboard: a SquareLine Studio LVGL UI showing the ESP-NOW payload
plus onboard GPS, IMU and environment sensors.

Verified working as of 2026-05 with **LVGL 9.5.0 + LovyanGFX 1.1.16** under the
full application (WiFi/ESP-NOW + LVGL). The three lessons that cost the most time
are at the top of "Hard-won constraints" below — read them before changing the
display stack.

## Board

| Field | Value |
|---|---|
| MCU | ESP32-S3, dual-core LX7 240 MHz |
| Flash / PSRAM | 16 MB QIO / OPI octal PSRAM 80 MHz (mandatory) |
| Panel | 7" 1024×600, 16-bit parallel RGB565, write-only |
| Touch | GT911 (I²C `0x5D`/`0x14`) — **does not answer on this unit** |
| I/O expander | PCF8574 @ I²C `0x24` |
| USB | native ESP32-S3 USB Serial/JTAG, but `ARDUINO_USB_CDC_ON_BOOT=0` (Serial = UART0 → CH343 bridge) |
| PlatformIO | `espressif32@6.9.0`, `board = esp32s3box`, Arduino, `qio_opi`, `default_16MB.csv` |

## Hard-won constraints

**LovyanGFX must be 1.1.x.** With `1.2.x` the identical `Panel_RGB`/`Bus_RGB`
configuration leaves the panel dark — backlight and I²C fine, no image ever.
Pinned as `lovyan03/LovyanGFX @ ^1.1.16`.

**`use_psram` must be 1.** `use_psram = 2` (tear-free double buffer) makes the
panel go completely dark the instant `lcd.init()` starts the double-buffer DMA.
Reproduced with a bare `fillScreen` before WiFi — so it is not LVGL, not radio
load, not USB supply. Every PCF8574 write still ACKs and P1 stays asserted.
Suspected regulator/backlight-converter limit on this board revision.
The cost of `use_psram = 1` is mild horizontal tearing (LVGL writes the live
framebuffer while the RGB peripheral scans it out), mitigated by small **partial**
flushes and by only repainting labels whose text changed. Tear-free operation is
not available on this board. Never use FULL refresh mode here — pushing the whole
1.2 MB frame maximises the contention window and makes tearing worse.

**Backlight is PCF8574 P1** (`0x02`), confirmed by an I²C bit-scan. `main.cpp`
still carries the pre-bit-scan `BL_BIT`/`TP_BIT` guesses marked WRONG — nothing
reads them; use the `PCF_*` constants. If the backlight will not light, redo the
bit-scan (write `0x01,0x02,0x04…0x80,0xFF`, 2.5 s each) before suspecting hardware.

## GPIO map

RGB data bus (RGB565 order: D0–D4 = Blue[3..7], D5–D10 = Green[2..7], D11–D15 = Red[3..7]):

| D0 | D1 | D2 | D3 | D4 | D5 | D6 | D7 | D8 | D9 | D10 | D11 | D12 | D13 | D14 | D15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 14 | 38 | 18 | 17 | 10 | 39 | 0 | 45 | 48 | 47 | 21 | 1 | 2 | 42 | 41 | 40 |

Verified by colour test: 14/38/18/17/10 → BLUE, 39/0/45/48/47/21 → GREEN,
1/2/42/41/40 → RED. Earlier Waveshare/ESPHome docs for other revisions have
channel-swapped labels; this mapping is the working one. GPIO 0, 3 and 46 are
strapping pins but safe as RGB outputs after boot.

| Signal | GPIO |
|---|---|
| PCLK / HSYNC / VSYNC / DE | 7 / 46 / 3 / 5 |
| I²C SDA / SCL | 8 / 9 (400 kHz) |
| GPS RX (UART0 RXD) | 44 |
| Brightness button | 6 |

Do **not** use the `I2C_SDA`/`I2C_SCL` macros — `esp32s3box/pins_arduino.h`
already defines them (SCL=18, SDA=8) and they collide.

### RGB timing (working values)

Pixel clock 16.5 MHz nominal (`freq_write = 16000000`), `pclk_idle_high = 0`.

| | polarity | front porch | pulse width | back porch |
|---|---|---|---|---|
| HSYNC | 0 (active LOW) | 40 | 48 | 88 |
| VSYNC | 0 (active LOW) | 3 | 10 | 18 |

Panel geometry: `memory_width/panel_width = 1024`, `memory_height/panel_height = 600`,
offsets 0/0. Start at 16 MHz PCLK — higher clocks corrupt the image at this PSRAM
bandwidth.

### PCF8574 (`0x24`)

| Pin | Mask | Signal | Active |
|---|---|---|---|
| P0 | `0x01` | LCD_RST | HIGH = running |
| P1 | `0x02` | LCD_BL | HIGH = ON |
| P2 | `0x04` | TP_RST | HIGH = running |

Operating value is `0x07`. Init order is load-bearing: I²C → PCF8574 outputs →
release LCD reset (`0x01`) → release touch reset (`0x05`) → backlight (`0x07`) →
LovyanGFX RGB panel init → render. Getting it wrong gives a black or white screen,
unstable sync, or a missing touch controller.

## GPS on the "UART2" connector

ATGM336H, NMEA 0183, 9600 8N1, 3.3 V. Both the board's "UART1" and "UART2"
connectors are physically **ESP32-S3 UART0 (GPIO 43/44)**; a DIP switch routes
UART0 either to the USB-C console *or* to the connector, never both. The firmware
reads GPIO 44 with the UART2 peripheral and renders the fix on the LCD.

Workflow: flash with the switch on **USB** (the CH343 download circuit needs
UART0), then flip to **UART2** to use the GPS. Flipping drops the PC serial
console — expected and unavoidable. Pin lives in `src/gps.h` (`GPS_RX_PIN`).

Other connectors are not plain UART: GPIO 15/16 = RS-485, GPIO 19/20 = CAN.

## Application layout (`src/`)

| File | Role |
|---|---|
| `main.cpp` | Board bring-up (PCF8574, LovyanGFX, LVGL), ESP-NOW, sensor polling, loop |
| `app_ui.{h,cpp}` | The **only** bridge to the SquareLine export in `src/ui/` |
| `ui/` | SquareLine Studio LVGL 9 export — **generated, never hand-edit** |
| `gps.{h,cpp}` | TinyGPSPlus on UART0/GPIO 44 |
| `gt911.{h,cpp}` | Capacitive touch (silent on this unit) |
| `mpu6050.{h,cpp}` | 6-axis IMU @ `0x68` |
| `aht20_bmp280.{h,cpp}` | AHT20 `0x38` + BMP280 `0x76/0x77` |
| `main_display.cpp`, `dashboard_ui.cpp`, `dashboard_widgets.cpp` | Legacy hand-written UI, excluded via `build_src_filter` |

`app_ui` keeps `main.cpp` decoupled from the widget tree: `create()`,
`update(const Payload&)`, `set_server_status(bool)`, plus setters for GPS, trip
time, ambient temperature, humidity and IMU. All must be called from the LVGL
thread.

**Brightness has no PWM path** on this board — PCF8574 P1 is on/off only. The
`StepBrightness` 10-step cycle is realised as the **opacity of a black overlay on
`lv_layer_top()`**. The on-screen button exists, but since the GT911 does not
answer here, the physical **button on GPIO 6** is what actually works.

`extra_scripts = pre:scripts/clean_dupes.py` runs before the build.

## LVGL 9 integration

```cpp
lv_display_t* disp = lv_display_create(1024, 600);
lv_display_set_flush_cb(disp, flush_cb);
lv_display_set_buffers(disp, buf1, buf2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
lv_indev_t* indev = lv_indev_create();
lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
lv_indev_set_read_cb(indev, touch_cb);
```

`LV_COLOR_DEPTH 16`, no colour swap (`LV_COLOR_16_SWAP` does not exist in LVGL 9 —
the panel takes native RGB565). Two DMA-capable PSRAM partial buffers (e.g.
1024×80), PARTIAL mode, flush via `lcd.pushImage(x, y, w, h, (uint16_t*)px_map)`
then `lv_display_flush_ready(disp)`.

Framebuffer is ~1.2 MB (RGB565 1024×600); internal SRAM cannot hold it, PSRAM is
mandatory. Target 20–40 FPS.

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| Dark after `lcd.init()` | Backlight/reset not initialised | Init PCF8574 first, in order |
| Dark, backlight + I²C OK | LovyanGFX 1.2.x | Pin to 1.1.16 |
| Dark, PCF writes ACK | `use_psram = 2` | Use `use_psram = 1` |
| Wrong colours | RGB mapping | Use the verified map above |
| Garbled image | Bad timings | Use the timings above |
| Tearing/flicker | Single framebuffer | Keep flushes small and partial |
| Image corruption at high clock | PSRAM bandwidth | Back down to 16 MHz PCLK |

Unsupported on an RGB panel: display readback, hardware screen capture, SPI LCD
commands, runtime `invertDisplay()`. TFT_eSPI is not usable; LovyanGFX only.
