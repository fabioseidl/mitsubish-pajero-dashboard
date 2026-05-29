# Waveshare ESP32-S3-Touch-LCD-7B — Hardware Specification

> Status: Verified working as of 2026-05. All parameters below produced a functional 1024×600 image with backlight operational, running **LVGL 9.5.0** + **LovyanGFX 1.1.16** under the full `main_display` application (WiFi/ESP-NOW + LVGL).
>
> Critical lessons learned this revision (details in the relevant sections):
> - **LovyanGFX must be 1.1.x** — `1.2.x` leaves the RGB panel dark (§7, §10).
> - **`use_psram` must be `1` on this board** — `use_psram=2` leaves the panel completely dark the instant its double-buffer DMA starts (reproduced with a direct draw before WiFi; not an LVGL/power issue), even though the PCF8574 enable stays asserted (§4, §9).
> - **Backlight = PCF8574 P1**, confirmed empirically by an I²C bit-scan (§5.2).
> - **LVGL 9.5.0** is the rendering stack (§15); the LVGL 8 API is no longer used.

---

# 1. Board Overview

| Field        | Value                                                   |
| ------------ | ------------------------------------------------------- |
| Manufacturer | Waveshare                                               |
| Model        | ESP32-S3-Touch-LCD-7B                                   |
| MCU          | ESP32-S3 (Xtensa LX7 dual-core, 240 MHz)                |
| Flash        | 16 MB, QIO mode                                         |
| PSRAM        | OPI Octal PSRAM, 80 MHz (mandatory for RGB framebuffer) |
| Display      | 7-inch RGB LCD, 1024 × 600 px                           |
| Touch IC     | GT911 (capacitive, I2C)                                 |
| I/O Expander | PCF8574 @ I²C `0x24`                                    |
| USB          | Native ESP32-S3 USB Serial/JTAG                         |

---

# 2. Display Panel

| Parameter                      | Value                                          |
| ------------------------------ | ---------------------------------------------- |
| Resolution                     | 1024 × 600 px                                  |
| Interface                      | 16-bit parallel RGB (RGB565)                   |
| `memory_width / memory_height` | 1024 / 600                                     |
| `panel_width / panel_height`   | 1024 / 600                                     |
| `offset_x / offset_y`          | 0 / 0                                          |
| Pixel clock                    | 16.5 MHz nominal                               |
| PCLK polarity                  | Active on falling edge (`pclk_active_neg = 1`) |

## 2.1 RGB Sync Timings

| Signal | Field               | Working Value  |
| ------ | ------------------- | -------------- |
| HSYNC  | `hsync_polarity`    | 0 (active LOW) |
| HSYNC  | `hsync_front_porch` | 40             |
| HSYNC  | `hsync_pulse_width` | 48             |
| HSYNC  | `hsync_back_porch`  | 88             |
| VSYNC  | `vsync_polarity`    | 0 (active LOW) |
| VSYNC  | `vsync_front_porch` | 3              |
| VSYNC  | `vsync_pulse_width` | 10             |
| VSYNC  | `vsync_back_porch`  | 18             |

---

# 3. GPIO Pin Mapping

## 3.1 RGB Bus (16-bit)

The ESP32-S3 LCD peripheral maps DATA[0..15] in RGB565 order:

* D0–D4 → Blue[3..7]
* D5–D10 → Green[2..7]
* D11–D15 → Red[3..7]

The following mapping is the verified working configuration for this board revision.

| Bus DATA | GPIO    | Physical Panel Color Channel | Description           |
| -------- | ------- | ---------------------------- | --------------------- |
| D0       | GPIO 14 | Blue 3 (LSB)                 | Lowest Blue bit used  |
| D1       | GPIO 38 | Blue 4                       | Blue data bit 4       |
| D2       | GPIO 18 | Blue 5                       | Blue data bit 5       |
| D3       | GPIO 17 | Blue 6                       | Blue data bit 6       |
| D4       | GPIO 10 | Blue 7 (MSB)                 | Highest Blue bit      |
| D5       | GPIO 39 | Green 2 (LSB)                | Lowest Green bit used |
| D6       | GPIO 0  | Green 3                      | Green data bit 3      |
| D7       | GPIO 45 | Green 4                      | Green data bit 4      |
| D8       | GPIO 48 | Green 5                      | Green data bit 5      |
| D9       | GPIO 47 | Green 6                      | Green data bit 6      |
| D10      | GPIO 21 | Green 7 (MSB)                | Highest Green bit     |
| D11      | GPIO 1  | Red 3 (LSB)                  | Lowest Red bit used   |
| D12      | GPIO 2  | Red 4                        | Red data bit 4        |
| D13      | GPIO 42 | Red 5                        | Red data bit 5        |
| D14      | GPIO 41 | Red 6                        | Red data bit 6        |
| D15      | GPIO 40 | Red 7 (MSB)                  | Highest Red bit       |

## 3.2 Verified Physical Routing

Color test validation confirms the following routing:

* GPIOs 14, 38, 18, 17, 10 → panel BLUE input
* GPIOs 39, 0, 45, 48, 47, 21 → panel GREEN input
* GPIOs 1, 2, 42, 41, 40 → panel RED input

This mapping matches the working LovyanGFX configuration and produces correct RGB color output without channel swapping.

> Note:
> Earlier documentation and ESPHome examples for some board revisions may contain incorrect color labels or channel-swapped assignments. The mapping above is the validated working configuration.

¹ GPIO 0, 3 and 46 are strapping pins. They are safe to use as RGB outputs after boot — the strapping values are sampled only during reset.

---

## 3.3 RGB Control Signals

| Signal | GPIO    |
| ------ | ------- |
| PCLK   | GPIO 7  |
| HSYNC  | GPIO 46 |
| VSYNC  | GPIO 3  |
| DE     | GPIO 5  |

---

## 3.4 I²C Bus

| Signal | GPIO    |
| ------ | ------- |
| SDA    | GPIO 8  |
| SCL    | GPIO 9  |
| Speed  | 400 kHz |

> Do not use the `I2C_SDA` / `I2C_SCL` macros from some board packages. Define explicit GPIO constants instead.

---

# 4. PSRAM Configuration

The 1024×600 RGB565 framebuffer requires approximately 1.2 MB per frame.

PSRAM is mandatory.

**Verified configuration for the `main_display` application:**

```cpp
cfg_detail.use_psram = 1;   // single framebuffer — REQUIRED here
```

Meaning:

* `0` = internal SRAM (insufficient — framebuffer is ~1.2 MB)
* `1` = single PSRAM framebuffer
* `2` = double framebuffer in PSRAM

`use_psram = 2` enables tear-free double-buffered DMA, but on this board it makes
**the panel go completely dark** the instant `lcd.init()` starts the
double-buffer DMA. This was reproduced with a **direct `fillScreen` color flash
before WiFi starts** (no LVGL, no radio load) — so it is **not** an LVGL/present
problem, **not** WiFi load, and **not** USB supply (occurs on a MacBook USB-C +
Apple cable at low activity). Every PCF8574 write still returns ACK and P1 stays
asserted; the panel/backlight simply will not light under the `use_psram=2` DMA.
Suspected cause: an internal regulator/backlight-converter limit or a LovyanGFX
1.1.16 `Panel_RGB` interaction specific to this board revision. See §9.

Therefore this board uses `use_psram = 1`. The trade-off is mild **horizontal
tearing** on a single live framebuffer (LVGL writes the buffer while the RGB
peripheral scans it out). Mitigated by (a) small partial-mode flushes and
(b) updating a label only when its text actually changes (skip no-op redraws).
Tear-free operation via `use_psram = 2` is **not available** on this board.

---

# 5. I/O Expander — PCF8574

## 5.1 Chip Identification

| Item        | Value                  |
| ----------- | ---------------------- |
| Chip        | PCF8574                |
| I²C address | `0x24`                 |
| Protocol    | Single-byte GPIO write |

Some newer board revisions use a PCF8574 instead of the CH422G mentioned in older documentation.

Expected I²C scan:

```text
[I2C] found 0x24
```

---

## 5.2 PCF8574 Pin Mapping

| PCF8574 Pin | Bit mask | Signal                  | Active level   |
| ----------- | -------- | ----------------------- | -------------- |
| P0          | `0x01`   | LCD_RST                 | HIGH = running |
| P1          | `0x02`   | LCD_BL                  | HIGH = ON      |
| P2          | `0x04`   | TP_RST                  | HIGH = running |
| P3–P7       | `0xF8`   | Unused / board-specific | —              |

> **Verified by I²C bit-scan:** writing `0x02` (P1 only) turns the backlight ON;
> the operating value `0x07` (`P0|P1|P2` = LCD out of reset + backlight + touch
> out of reset) is correct. The I²C scan finds exactly one device, `0x24`
> (the GT911 touch controller does not appear until its own init sequence runs).
> If the backlight will not light, run a bit-scan (write `0x01,0x02,0x04…0x80,0xFF`,
> 2.5 s each, watch the panel) before assuming a wiring/chip fault.

---

## 5.3 Minimal Init Sequence

```cpp
// All reset/off
Wire.beginTransmission(0x24);
Wire.write(0x00);
Wire.endTransmission();
delay(20);

// LCD reset release
Wire.beginTransmission(0x24);
Wire.write(0x01);
Wire.endTransmission();
delay(10);

// Touch reset release
Wire.beginTransmission(0x24);
Wire.write(0x05);
Wire.endTransmission();
delay(50);

// Backlight ON
Wire.beginTransmission(0x24);
Wire.write(0x07);
Wire.endTransmission();
```

---

# 6. Touch Controller — GT911

| Item      | Value             |
| --------- | ----------------- |
| IC        | Goodix GT911      |
| Interface | I²C               |
| Address   | `0x5D` or `0x14`  |
| Reset     | PCF8574 P2        |
| INT pin   | Project-dependent |

---

# 7. LovyanGFX Configuration Summary

> **Pin LovyanGFX to 1.1.x** (`lovyan03/LovyanGFX @ ~1.1.16`). With `1.2.x` the
> exact same `Panel_RGB` / `Bus_RGB` configuration below leaves the panel dark
> (backlight + I²C are fine; the panel never produces an image). `1.1.16` is the
> verified version.

Required includes:

```cpp
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
```

Complete working configuration:

```cpp
class LGFX : public lgfx::LGFX_Device {
public:
  lgfx::Bus_RGB   _bus_instance;
  lgfx::Panel_RGB _panel_instance;

  LGFX() {

    auto cfg_p = _panel_instance.config();
    cfg_p.memory_width  = 1024;
    cfg_p.memory_height = 600;
    cfg_p.panel_width   = 1024;
    cfg_p.panel_height  = 600;
    cfg_p.offset_x = 0;
    cfg_p.offset_y = 0;
    _panel_instance.config(cfg_p);

    auto cfg_d = _panel_instance.config_detail();
    cfg_d.use_psram = 1;   // see §4 — must be 1 under full app load (backlight)
    _panel_instance.config_detail(cfg_d);

    auto cfg = _bus_instance.config();
    cfg.panel = &_panel_instance;

    cfg.pin_d0  = GPIO_NUM_14;
    cfg.pin_d1  = GPIO_NUM_38;
    cfg.pin_d2  = GPIO_NUM_18;
    cfg.pin_d3  = GPIO_NUM_17;
    cfg.pin_d4  = GPIO_NUM_10;

    cfg.pin_d5  = GPIO_NUM_39;
    cfg.pin_d6  = GPIO_NUM_0;
    cfg.pin_d7  = GPIO_NUM_45;
    cfg.pin_d8  = GPIO_NUM_48;
    cfg.pin_d9  = GPIO_NUM_47;
    cfg.pin_d10 = GPIO_NUM_21;

    cfg.pin_d11 = GPIO_NUM_1;
    cfg.pin_d12 = GPIO_NUM_2;
    cfg.pin_d13 = GPIO_NUM_42;
    cfg.pin_d14 = GPIO_NUM_41;
    cfg.pin_d15 = GPIO_NUM_40;

    cfg.pin_henable = GPIO_NUM_5;
    cfg.pin_vsync   = GPIO_NUM_3;
    cfg.pin_hsync   = GPIO_NUM_46;
    cfg.pin_pclk    = GPIO_NUM_7;

    cfg.freq_write = 16000000;

    cfg.pclk_idle_high = 0;

    cfg.hsync_polarity    = 0;
    cfg.hsync_front_porch = 40;
    cfg.hsync_pulse_width = 48;
    cfg.hsync_back_porch  = 88;

    cfg.vsync_polarity    = 0;
    cfg.vsync_front_porch = 3;
    cfg.vsync_pulse_width = 10;
    cfg.vsync_back_porch  = 18;

    _bus_instance.config(cfg);
    _panel_instance.setBus(&_bus_instance);
    setPanel(&_panel_instance);
  }
};
```

---

# 8. PlatformIO / Arduino Environment Notes

| Item                              | Recommended value                    |
| --------------------------------- | ------------------------------------ |
| Board                             | `esp32s3box` or `esp32-s3-devkitc-1` |
| Framework                         | Arduino                              |
| `board_build.arduino.memory_type` | `qio_opi`                            |
| `board_upload.flash_size`         | `16MB`                               |

---

# 9. Known Pitfalls

| Pitfall                        | Cause                           | Fix                          |
| ------------------------------ | ------------------------------- | ---------------------------- |
| Screen dark after `lcd.init()` | Backlight/reset not initialized | Initialize PCF8574 first     |
| Panel dark, backlight + I²C OK | LovyanGFX 1.2.x                  | Pin LovyanGFX to 1.1.x (§7)  |
| Panel dark, PCF writes ACK     | `use_psram=2` (panel won't light under double-buffer DMA, even pre-WiFi) | Use `use_psram=1` (§4) |
| Wrong colors                   | Incorrect RGB mapping           | Use verified mapping from §3 |
| Garbled image                  | Invalid timing values           | Use timings from §2.1        |
| Horizontal tearing/flicker     | Single framebuffer (`use_psram=1`) | Keep flushes small (partial); stronger PSU for `use_psram=2` |
| PSRAM crashes                  | Incorrect memory config         | Enable OPI PSRAM             |
| I²C macro conflicts            | Board package GPIO defines      | Use explicit GPIO constants  |
| Image corruption at high clock | PSRAM bandwidth limit           | Start with 16 MHz PCLK       |

# 10. Software Compatibility Matrix

| Component     | Tested Version | Status                                   |
| ------------- | -------------- | ---------------------------------------- |
| Arduino-ESP32 | 3.x (platform espressif32@6.9.0) | Working                |
| LovyanGFX     | 1.1.16         | Working — **required**; 1.2.x = dark panel |
| ESP-IDF       | 5.x            | Untested                                 |
| LVGL          | 9.5.0          | Working (current stack)                  |
| LVGL          | 8.x            | Legacy — no longer used                  |
| TFT_eSPI      | —              | Not recommended                          |
| PlatformIO    | Latest         | Working                                  |

---

# 11. Initialization Order (Critical)

The board requires this initialization sequence:

1. Initialize I²C bus
2. Initialize PCF8574 outputs
3. Release LCD reset
4. Release touch reset
5. Enable backlight
6. Initialize LovyanGFX RGB panel
7. Start rendering

Incorrect order may produce:

* black screen;
* white screen;
* unstable sync;
* touch controller missing.

---

# 12. RGB Panel Characteristics

| Item                      | Value                |
| ------------------------- | -------------------- |
| Interface type            | RGB565 parallel      |
| Readback support          | No                   |
| Runtime commands          | Limited              |
| `invertDisplay()` support | Usually ignored      |
| DMA framebuffer           | Required             |
| Double buffering          | Recommended          |
| Tear-free rendering       | Supported with PSRAM |

Important:
This panel is not SPI-based. Most runtime LCD commands are unavailable or ignored.

---

# 13. Touch Coordinate Notes

| Item                 | Value               |
| -------------------- | ------------------- |
| Touch IC             | GT911               |
| Native orientation   | Landscape           |
| Coordinate inversion | Depends on mounting |
| Multi-touch          | Supported           |
| Typical max points   | 5                   |

Recommended calibration test:

* draw crosshair at touch position;
* verify axis orientation;
* verify X/Y swap.

---

# 14. Performance Recommendations

| Feature                | Recommended       |
| ---------------------- | ----------------- |
| Target FPS             | 20–40 FPS         |
| Safe pixel clock       | 16 MHz            |
| Aggressive pixel clock | 20–30 MHz         |
| Full-screen redraw     | Avoid if possible |
| Partial redraw         | Recommended       |
| PSRAM usage            | Mandatory         |

---

# 15. LVGL Integration Notes (LVGL 9.5.0)

Verified LVGL settings (`lv_conf.h`):

```cpp
#define LV_COLOR_DEPTH 16
// LV_COLOR_16_SWAP does not exist in LVGL 9 — the panel uses native RGB565
// (no swap). Pixels are pushed unswapped to LovyanGFX via pushImage().
```

LVGL 9 display/input registration (the LVGL 8 `lv_disp_drv_t` / `lv_indev_drv_t`
APIs are gone):

```cpp
lv_display_t* disp = lv_display_create(1024, 600);
lv_display_set_flush_cb(disp, flush_cb);                 // cb: (lv_display_t*, const lv_area_t*, uint8_t* px_map)
lv_display_set_buffers(disp, buf1, buf2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

lv_indev_t* indev = lv_indev_create();
lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
lv_indev_set_read_cb(indev, touch_cb);                   // cb: (lv_indev_t*, lv_indev_data_t*)
```

Framebuffer strategy (verified):

* DMA-capable PSRAM partial buffers (e.g. two buffers of 1024 × 80 px);
* **PARTIAL** render mode — smallest flushes minimise tearing on the single
  framebuffer (`use_psram=1`, see §4);
* flush pushes RGB565 via `lcd.pushImage(x, y, w, h, (uint16_t*)px_map)` then
  `lv_display_flush_ready(disp)`;
* advance time with `lv_tick_inc(elapsed_ms)` and run `lv_timer_handler()` from
  the loop task. All LVGL calls must stay on one task (it is not thread-safe);
  buffer ESP-NOW payloads from the WiFi task and apply them in the loop.

> Do **not** use FULL refresh mode with `use_psram=1`: pushing the whole 1.2 MB
> frame each update maximises the CPU/DMA contention window and worsens tearing.

---

# 16. Known Unsupported Features

| Feature                 | Status              |
| ----------------------- | ------------------- |
| Display readback        | Unsupported         |
| Hardware screen capture | Unsupported         |
| SPI LCD commands        | Unsupported         |
| Runtime inversion       | Usually unsupported |
| Hardware brightness PWM | Board-dependent     |

---

# 17. Memory Usage Reference

Approximate framebuffer sizes:

| Format             | Size    |
| ------------------ | ------- |
| RGB565 1024×600    | ~1.2 MB |
| Double framebuffer | ~2.4 MB |

Approximate available memory:

* Internal SRAM insufficient for framebuffer;
* PSRAM mandatory.

---

# 18. Recommended LLM Assumptions

LLMs generating applications for this board should assume:

* RGB565 framebuffer rendering;
* write-only RGB display;
* PSRAM available;
* LovyanGFX preferred;
* GT911 touch via I²C;
* PCF8574 backlight/reset control;
* landscape orientation;
* 1024×600 resolution;
* DMA rendering enabled.

LLMs should NOT assume:

* SPI display compatibility;
* TFT_eSPI compatibility;
* runtime display command support;
* framebuffer readback capability.
