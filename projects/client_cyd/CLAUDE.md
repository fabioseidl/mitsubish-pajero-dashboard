# CLAUDE.md — projects/client_cyd

Read the root `CLAUDE.md` and `docs/04_client_lib_spec.md` before working on this project.

---

## Purpose

Receives `Payload` broadcasts from the server (or emulator) via ESP-NOW and renders the data on a CYD 2.4" ILI9341 320x240 display using LVGL. Supports touch-based brightness cycling across 4 levels (25%, 50%, 75%, 100%).

---

## Hardware

| Component | Detail |
|---|---|
| MCU | ESP32 (CYD board) |
| Display | ILI9341, 2.4", 320x240px, SPI |
| Touch | XPT2046, resistive, SPI |
| Backlight | GPIO 21, LEDC PWM |

All GPIO numbers must be defined as named constants in `include/pin_config.h`. Never use raw numbers inline.

---

## Classes in This Project

| Class | File | Responsibility |
|---|---|---|
| `CYDScreenController` | `include/cyd_screen_controller.h` | LVGL layout, widget updates, touch polling |

Classes from `lib/core` also used here: `CYDDisplay`, `BrightnessController`, `ESPNowReceiver`, `IScreenController`, `Payload`.

---

## Client Role — Pure Renderer

This client has no business logic. It does not:
- Compute consumption, distance, or any derived value
- Filter or validate sensor data beyond what `ESPNowReceiver` already validates
- Store any state between received payloads beyond what LVGL widget state requires

All values displayed come directly from the received `Payload` fields. If a value is 0.0, display 0.0.

---

## Display Layout — CYD 2.4" (320x240)

The layout is fixed and defined at development time. Users cannot change it.

Widgets to implement:

| Widget | Payload field | Display format |
|---|---|---|
| RPM arc + label | `payload.rpm` | Arc 0–6000, label "XXXX rpm" |
| Speed label | `payload.speed_kmh` | "XXX km/h" |
| Instantaneous consumption | `payload.consumption_km_per_l` | "XX.X km/L" |
| Average consumption | `payload.avg_consumption_km_per_l` | "XX.X km/L avg" |
| Session distance | `payload.distance_km` | "XXX.X km" |
| MIL indicator | `payload.mil_on` | Colored dot or label — red if true, hidden if false |

---

## LVGL Rules

- LVGL version: **8.x** — verify exact version in `platformio.ini` before using any API
- `lv_init()` must be called once in `CYDScreenController::begin()` before any widget creation
- All widget creation and updates must happen from the same FreeRTOS task — LVGL is not thread-safe
- `lv_timer_handler()` must be called at least every 5 ms from `CYDScreenController::tick()`
- `lv_tick_inc(delta_ms)` must be fed accurate elapsed milliseconds — use `esp_timer_get_time()` difference
- The `onPayloadReceived()` callback runs in ESP-NOW ISR context — it must be fast and non-blocking. Only update widget values here; do not call `lv_timer_handler()`

---

## Brightness Control

- Touch anywhere on screen → `BrightnessController::onTouch()` → next brightness level
- Level cycle: 25% → 50% → 75% → 100% → 25% → ...
- Initial level on boot: 75% (index 2)
- XPT2046 is polled inside `CYDScreenController::tick()`
- Debounce touch events — do not advance brightness on every tick while finger is held down
- Suggested debounce: ignore touch events within 300 ms of the last brightness change

---

## Receiving Data

- `ESPNowReceiver::begin()` is called with the PMK from `security_config.h`
- `ESPNowReceiver::setCallback()` is called with a lambda that calls `screen.onPayloadReceived(p)`
- The receiver validates payload version, size, and `PAYLOAD_FLAG_DATA_VALID` before invoking the callback — no additional validation needed in the client

---

## Tests for This Project

`CYDScreenController` is excluded from host tests (LVGL and hardware dependency). Brightness logic is fully tested via `BrightnessController` tests in `test/host/lib/test_brightness_controller.cpp`.

`ESPNowReceiver` receive logic is tested in `test/host/lib/test_espnow_receiver.cpp`.

---

## Build

```bash
cd projects/client_cyd
pio run
pio run --target upload
```

During client development, flash `projects/server_emulator` to a second ESP32 to provide data without needing the vehicle.
