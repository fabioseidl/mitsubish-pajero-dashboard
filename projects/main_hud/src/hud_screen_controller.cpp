#include "hud_screen_controller.h"

#ifndef UNIT_TEST

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>

#include "pin_config.h"

// ── Arduino_GFX stack ────────────────────────────────────────────────────────
// LVGL draws into the canvas; gfx->flush() ships the whole framebuffer to the
// panel over QSPI. The canvas is what makes LVGL's partial rendering usable
// here: the AXS15231B mishandles partial window writes, but the canvas absorbs
// the partial updates and always pushes a complete frame.
static Arduino_DataBus* s_bus    = nullptr;
static Arduino_GFX*     s_panel  = nullptr;
static Arduino_Canvas*  s_canvas = nullptr;

HudScreenController* HudScreenController::instance_ = nullptr;

HudScreenController::HudScreenController(StepBrightness& brightness)
    : brightness_(brightness),
      touch_(GPIO_TOUCH_SCL, GPIO_TOUCH_SDA, GPIO_TOUCH_INT, TOUCH_I2C_ADDR) {}

// ── Init ─────────────────────────────────────────────────────────────────────

bool HudScreenController::begin() {
    instance_ = this;

    if (!initDisplay()) return false;

    // Touch is not load-bearing: without it you lose the two buttons, but the
    // speed readout — the reason the device exists — still works. Don't let a
    // touch fault take the display down with it.
    if (!touch_.begin()) {
        Serial.println("[SCREEN] touch init failed — buttons disabled, speed still shown");
    }

    Serial.println("[SCREEN] init LVGL...");
    if (!initLvgl()) return false;
    Serial.println("[SCREEN] LVGL OK — building UI");

    // Buttons start visible so they can be found; tick() hides them once
    // BUTTONS_IDLE_HIDE_MS passes with no touch.
    ui_.build(lv_screen_active(), onBrightnessUp, onBrightnessDown, this);
    refreshBrightnessLabel();

    // Start the auto-hide countdown from when the UI actually appears, not from
    // boot: setup() spends ~2 s before this point, which would eat most of the
    // window and hide the buttons before they had been seen.
    last_touch_ms_ = millis();
    return true;
}

bool HudScreenController::initDisplay() {
    Serial.println("[SCREEN] creating QSPI bus...");
    s_bus = new Arduino_ESP32QSPI(GPIO_LCD_CS, GPIO_LCD_SCK,
                                  GPIO_LCD_D0, GPIO_LCD_D1, GPIO_LCD_D2, GPIO_LCD_D3);

    Serial.println("[SCREEN] creating AXS15231B panel...");
    // No reset line is broken out on this board, and the glass is not IPS-inverted.
    s_panel  = new Arduino_AXS15231B(s_bus, GFX_NOT_DEFINED, 0, false,
                                     PANEL_NATIVE_W, PANEL_NATIVE_H);

    Serial.println("[SCREEN] creating canvas...");
    s_canvas = new Arduino_Canvas(PANEL_NATIVE_W, PANEL_NATIVE_H, s_panel, 0, 0, SCREEN_ROTATION);

    Serial.println("[SCREEN] canvas->begin() — QSPI init + 307 KB framebuffer alloc...");
    // Fails either because the panel didn't answer on QSPI, or because the
    // 307 KB framebuffer couldn't be allocated (i.e. PSRAM is missing).
    if (!s_canvas->begin(40000000UL)) {
        Serial.println("[SCREEN] canvas/panel init failed (QSPI init or 307 KB framebuffer alloc)");
        return false;
    }
    Serial.println("[SCREEN] canvas->begin() OK");
    s_canvas->fillScreen(BLACK);
    s_canvas->flush();

    Serial.printf("[SCREEN] display %dx%d ready\n", s_canvas->width(), s_canvas->height());
    return true;
}

bool HudScreenController::initLvgl() {
    lv_init();
    lv_tick_set_cb(tickCb);

    lv_display_t* disp = lv_display_create(SCREEN_W, SCREEN_H);
    lv_display_set_flush_cb(disp, flushCb);

    // Tenth-of-a-screen partial buffer in internal RAM — DMA-capable and much
    // faster than PSRAM for LVGL's read-modify-write rendering. The canvas
    // framebuffer behind it lands in PSRAM (307 KB won't fit internally).
    const uint32_t buf_px    = (uint32_t)SCREEN_W * SCREEN_H / 10;
    const uint32_t buf_bytes = buf_px * sizeof(uint16_t);
    void* draw_buf = heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!draw_buf) {
        Serial.println("[SCREEN] LVGL draw buffer alloc failed");
        return false;
    }
    lv_display_set_buffers(disp, draw_buf, nullptr, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchReadCb);
    return true;
}

// ── UI ───────────────────────────────────────────────────────────────────────

void HudScreenController::refreshBrightnessLabel() {
    ui_.setBrightnessPercent(brightness_.getCurrentPercent());
}

// ── LVGL callbacks ───────────────────────────────────────────────────────────

uint32_t HudScreenController::tickCb() {
    return millis();
}

void HudScreenController::flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    const int32_t w = lv_area_get_width(area);
    const int32_t h = lv_area_get_height(area);
    s_canvas->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)px_map, w, h);

    instance_->canvas_dirty_ = true;
    lv_display_flush_ready(disp);
}

void HudScreenController::touchReadCb(lv_indev_t* indev, lv_indev_data_t* data) {
    HudScreenController* self = instance_;
    const uint32_t now = millis();

    uint16_t x, y;
    if (self->touch_.read(&x, &y)) {
        // A lapsed hold window means the finger had lifted, so this report
        // starts a new gesture. Decide once, here, whether it is a wake-up tap:
        // deciding per-report would flip mid-gesture as the buttons appear.
        if (now >= self->touch_until_ms_) {
            self->wake_only_ = !self->ui_.buttonsVisible();
        }

        self->touch_x_ = x;
        self->touch_y_ = y;
        self->touch_until_ms_ = now + TOUCH_HOLD_MS;
        self->last_touch_ms_  = now;  // tick() uses this to time the auto-hide
    }

    const bool pressed = now < self->touch_until_ms_;

    data->point.x = self->touch_x_;
    data->point.y = self->touch_y_;
    // A wake-up gesture is swallowed entirely: it brings the buttons back but
    // must not click the one it landed on.
    data->state = (pressed && !self->wake_only_) ? LV_INDEV_STATE_PRESSED
                                                 : LV_INDEV_STATE_RELEASED;
}

void HudScreenController::onBrightnessUp(void* ctx) {
    HudScreenController* self = static_cast<HudScreenController*>(ctx);
    self->brightness_.increase();
    self->refreshBrightnessLabel();
}

void HudScreenController::onBrightnessDown(void* ctx) {
    HudScreenController* self = static_cast<HudScreenController*>(ctx);
    self->brightness_.decrease();
    self->refreshBrightnessLabel();
}

// ── Data in ──────────────────────────────────────────────────────────────────

void HudScreenController::onPayloadReceived(const Payload& payload) {
    // Runs in the ESP-NOW callback: just record, let tick() touch the widgets.
    // LVGL is not reentrant and must only be driven from the loop task.
    speed_kmh_ = payload_speed_kmh_u8(&payload);
}

void HudScreenController::onServerStatusChanged(bool online) {
    online_ = online;
}

void HudScreenController::tick() {
    // Snapshot once: the ESP-NOW callback can land between the two reads.
    const uint8_t speed  = speed_kmh_;
    const bool    online = online_;
    if (speed != shown_speed_ || online != shown_online_) {
        shown_speed_  = speed;
        shown_online_ = online;
        ui_.setSpeed(speed, online);
    }

    // Auto-hide the buttons once the screen has been left alone, so normal
    // driving shows nothing but the speed. Evaluated before lv_task_handler()
    // so the visibility LVGL hit-tests against is the one the user can see.
    ui_.setButtonsVisible((millis() - last_touch_ms_) < BUTTONS_IDLE_HIDE_MS);

    lv_task_handler();  // may call flushCb, which raises canvas_dirty_

    if (canvas_dirty_) {
        s_canvas->flush();
        canvas_dirty_ = false;
    }
}

#endif // UNIT_TEST
