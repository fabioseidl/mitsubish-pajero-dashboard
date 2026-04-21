# Client Shared Library Specification

## Library: `lib/core`

Shared by all client projects. Provides ESP-NOW reception, display abstraction, and brightness control. Each client project depends on this library and implements its own LVGL screen layout on top of it.

---

## Classes

### `IDisplay` (interface)

Abstract interface for display hardware control. Exists to decouple brightness logic and LVGL initialization from hardware specifics, enabling host-side testing with mock implementations.

```cpp
// lib/core/include/i_display.h

class IDisplay {
public:
    virtual ~IDisplay() = default;

    /**
     * Initializes display hardware and backlight PWM peripheral.
     * Must be called before setBacklightPercent().
     *
     * @return true  Hardware initialized successfully.
     * @return false Initialization failed.
     */
    virtual bool begin() = 0;

    /**
     * Sets display backlight brightness.
     *
     * @param percent Brightness level from 0 to 100.
     *                Values above 100 are clamped to 100.
     */
    virtual void setBacklightPercent(uint8_t percent) = 0;
};
```

---

### `CYDDisplay : IDisplay`

Concrete implementation for the CYD (Cheap Yellow Display) ESP32 board with ILI9341 2.4" display. Controls backlight via ESP-IDF LEDC PWM peripheral.

```cpp
// lib/core/include/cyd_display.h

class CYDDisplay : public IDisplay {
public:
    /**
     * @param backlight_pin GPIO number for backlight PWM output.
     *                      On standard CYD boards this is GPIO 21.
     * @param pwm_channel   LEDC channel to use (0–7).
     *                      Default: 0. Change if channel conflicts with other peripherals.
     */
    explicit CYDDisplay(uint8_t backlight_pin, uint8_t pwm_channel = 0);

    /**
     * Configures the LEDC timer and channel for backlight PWM at 5 kHz,
     * 13-bit resolution. Sets initial brightness to 75%.
     *
     * @return true  LEDC configured successfully.
     * @return false LEDC configuration failed.
     */
    bool begin() override;

    /**
     * Updates the LEDC duty cycle to achieve the requested brightness.
     *
     * Mapping: duty = (percent / 100.0f) * 8191  (13-bit range: 0–8191)
     *
     * @param percent Brightness from 0 to 100. Clamped to [0, 100].
     */
    void setBacklightPercent(uint8_t percent) override;

private:
    uint8_t backlight_pin_;
    uint8_t pwm_channel_;

    /**
     * Converts a percentage value to a 13-bit LEDC duty cycle integer.
     *
     * @param percent Brightness percentage (0–100).
     * @return LEDC duty value in range [0, 8191].
     */
    uint32_t percentToDuty(uint8_t percent) const;
};
```

---

### `BrightnessController`

Manages a 4-level brightness cycle triggered by touch events. Advances the level on each call to `onTouch()` and applies the new brightness to the display.

Touch debouncing is the caller's responsibility. `BrightnessController` does not debounce internally.

```cpp
// lib/core/include/brightness_controller.h

class BrightnessController {
public:
    static constexpr uint8_t LEVEL_COUNT = 4;

    // Brightness percent values for levels 0–3
    static constexpr uint8_t LEVEL_PERCENTS[LEVEL_COUNT] = {25, 50, 75, 100};

    /**
     * @param display Reference to an IDisplay implementation.
     *                The display must outlive this controller.
     */
    explicit BrightnessController(IDisplay& display);

    /**
     * Advances brightness to the next level (wraps from level 3 back to level 0)
     * and immediately applies it by calling display.setBacklightPercent().
     *
     * Level sequence: 0(25%) → 1(50%) → 2(75%) → 3(100%) → 0(25%) → ...
     */
    void onTouch();

    /**
     * Returns the current brightness level index.
     *
     * @return Level index in range [0, 3].
     */
    uint8_t getCurrentLevel() const;

    /**
     * Returns the current brightness as a percentage.
     *
     * @return One of: 25, 50, 75, 100.
     */
    uint8_t getCurrentPercent() const;

    /**
     * Applies the current brightness level to the display without advancing.
     * Must be called once during initialization after display.begin().
     */
    void applyInitial();

private:
    IDisplay& display_;
    uint8_t   current_level_; // Initialized to 2 (75%)
};
```

---

### `ESPNowReceiver`

Manages ESP-NOW initialization on the client side and dispatches validated incoming `Payload` structs to a registered callback. Implemented as a singleton because ESP-NOW's C receive callback cannot carry user context.

```cpp
// lib/core/include/espnow_receiver.h

using PayloadCallback = void (*)(const Payload& payload);

class ESPNowReceiver {
public:
    ESPNowReceiver() = default;

    /**
     * Initializes Wi-Fi in station mode, initializes ESP-NOW,
     * and sets the PMK for decryption.
     *
     * Registers onReceiveISR as the ESP-NOW receive callback.
     * Must be called once before setCallback().
     *
     * @param pmk 16-byte Primary Master Key. Must match the server's PMK.
     * @return true  ESP-NOW initialized and PMK set.
     * @return false Initialization failed.
     */
    bool begin(const uint8_t pmk[16]);

    /**
     * Registers the application callback to be invoked on each valid payload.
     *
     * The callback is invoked from the ESP-NOW receive callback context
     * (ISR-adjacent). Callback implementations must be short and non-blocking.
     *
     * @param cb Function pointer to invoke with each received Payload.
     *           Pass nullptr to deregister.
     */
    void setCallback(PayloadCallback cb);

private:
    static ESPNowReceiver* instance_; // Singleton pointer for C callback access

    PayloadCallback callback_;

    /**
     * Registered with esp_now_register_recv_cb. Validates the incoming data
     * and invokes callback_ if all checks pass.
     *
     * Validation rules:
     *   1. len == sizeof(Payload)
     *   2. payload.version == PAYLOAD_VERSION
     *   3. (payload.flags & PAYLOAD_FLAG_DATA_VALID) != 0
     *
     * Payloads failing any check are silently discarded.
     *
     * @param mac  Source MAC address (unused).
     * @param data Raw received bytes.
     * @param len  Number of bytes received.
     */
    static void onReceiveISR(const uint8_t* mac,
                              const uint8_t* data,
                              int len);
};
```

---

### `IScreenController` (interface)

Abstract base for client-specific screen implementations. Each client project subclasses this to define its LVGL layout and widget update logic.

```cpp
// lib/core/include/i_screen_controller.h

class IScreenController {
public:
    virtual ~IScreenController() = default;

    /**
     * Initializes LVGL, registers display and touch input drivers,
     * creates all screen widgets, and applies initial brightness.
     *
     * Must be called once before tick() or onPayloadReceived().
     *
     * @return true  LVGL and all drivers initialized successfully.
     * @return false Initialization failed.
     */
    virtual bool begin() = 0;

    /**
     * Called by ESPNowReceiver callback when a new Payload is received.
     * Implementations update LVGL widget values from payload fields.
     *
     * Must be non-blocking. Must not call lv_timer_handler() internally.
     *
     * @param payload Validated Payload from the server or emulator.
     */
    virtual void onPayloadReceived(const Payload& payload) = 0;

    /**
     * Called from the main loop at regular intervals.
     * Responsible for:
     *   - Calling lv_timer_handler() to process LVGL render queue
     *   - Feeding lv_tick_inc() with elapsed milliseconds
     *   - Polling XPT2046 touch input and forwarding to BrightnessController
     */
    virtual void tick() = 0;
};
```

---

## How a Client Project Uses This Library

```cpp
// projects/client_cyd/src/main.cpp

#include "cyd_display.h"
#include "brightness_controller.h"
#include "espnow_receiver.h"
#include "cyd_screen_controller.h" // defined in client project
#include "security_config.h"

static CYDDisplay          display(GPIO_BACKLIGHT_PIN);
static BrightnessController brightness(display);
static ESPNowReceiver      receiver;
static CYDScreenController screen(brightness);

void app_main() {
    display.begin();
    brightness.applyInitial();
    screen.begin();

    receiver.setCallback([](const Payload& p) {
        screen.onPayloadReceived(p);
    });
    receiver.begin(PMK_KEY);

    while (true) {
        screen.tick();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
```

---

## Client Project: `CYDScreenController`

Defined in `projects/client_cyd/include/cyd_screen_controller.h`. Not part of `lib/core`. Implements `IScreenController` for the CYD 2.4" layout.

```cpp
// projects/client_cyd/include/cyd_screen_controller.h

class CYDScreenController : public IScreenController {
public:
    /**
     * @param brightness Reference to BrightnessController for touch-driven
     *                   backlight cycling. Must outlive this controller.
     */
    explicit CYDScreenController(BrightnessController& brightness);

    /**
     * Initializes LVGL, ILI9341 display flush driver, XPT2046 touch driver,
     * and creates all widgets for the CYD layout:
     *   - RPM arc/label
     *   - Speed label
     *   - Instantaneous consumption label
     *   - Average consumption label
     *   - Distance label
     *   - MIL indicator
     *
     * @return true  All initialization succeeded.
     * @return false LVGL or driver initialization failed.
     */
    bool begin() override;

    /**
     * Updates all LVGL widget values from the received payload.
     * Called from the ESP-NOW receive callback context — must be fast.
     *
     * @param payload Validated Payload containing all display metrics.
     */
    void onPayloadReceived(const Payload& payload) override;

    /**
     * Advances LVGL tick counter, runs lv_timer_handler(),
     * polls XPT2046 for touch events, and forwards touches to BrightnessController.
     */
    void tick() override;

private:
    BrightnessController& brightness_;

    // LVGL widget handles — created in begin(), updated in onPayloadReceived()
    lv_obj_t* rpm_arc_;
    lv_obj_t* rpm_label_;
    lv_obj_t* speed_label_;
    lv_obj_t* consumption_label_;
    lv_obj_t* avg_consumption_label_;
    lv_obj_t* distance_label_;
    lv_obj_t* mil_indicator_;

    uint32_t last_tick_ms_;
};
```

---

## LVGL Integration Notes

- LVGL version: **8.x** (verify exact version in `platformio.ini` before coding)
- `lv_init()` must be called before any widget creation
- Display driver: `lv_disp_drv_t` with ILI9341 SPI flush callback
- Input driver: `lv_indev_drv_t` with XPT2046 read callback
- `lv_tick_inc(delta_ms)` must be called from a reliable timer source (FreeRTOS tick or `esp_timer`)
- `lv_timer_handler()` must be called at least every 5 ms from `tick()`
- All widget creation and updates must occur from the same task (LVGL is not thread-safe)
