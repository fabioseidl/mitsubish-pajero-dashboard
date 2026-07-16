#include "hud_screen_controller.h"

#ifndef UNIT_TEST

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>

#include "pin_config.h"

// Roboto Bold, generated from ui/client_simple_hud/assets/Roboto-Bold.ttf by
// lv_font_conv (see CLAUDE.md). The 200 px face carries digits and '-' only.
extern const lv_font_t ui_font_roboto_bold_200;
extern const lv_font_t ui_font_roboto_bold_28;

// ── Arduino_GFX stack ────────────────────────────────────────────────────────
// LVGL draws into the canvas; gfx->flush() ships the whole framebuffer to the
// panel over QSPI. The canvas is what makes LVGL's partial rendering usable
// here: the AXS15231B mishandles partial window writes, but the canvas absorbs
// the partial updates and always pushes a complete frame.
static Arduino_DataBus* s_bus    = nullptr;
static Arduino_GFX*     s_panel  = nullptr;
static Arduino_Canvas*  s_canvas = nullptr;

// One row of mirrored pixels, reused every flush in HUD mode.
static uint16_t s_mirror_row[SCREEN_W];

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

    buildUi();

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

static constexpr int16_t BTN_MARGIN = 8;

// Places a button at a corner described in *physical* terms — the corner the
// driver sees — and cancels out whatever flip the current mode applies. In HUD
// mode the frame is transformed on its way to the panel, so a widget LVGL puts
// at the top-right would surface at the physical bottom-left; asking for the
// opposite corner in LVGL's space undoes that, and the buttons stay put across
// a mode switch.
static void alignPhysicalCorner(lv_obj_t* obj, bool want_top, bool want_right,
                                bool flip_x, bool flip_y) {
    const bool top   = want_top   != flip_y;
    const bool right = want_right != flip_x;

    const lv_align_t align = top ? (right ? LV_ALIGN_TOP_RIGHT    : LV_ALIGN_TOP_LEFT)
                                 : (right ? LV_ALIGN_BOTTOM_RIGHT : LV_ALIGN_BOTTOM_LEFT);
    lv_obj_align(obj, align, right ? -BTN_MARGIN : BTN_MARGIN,
                             top   ?  BTN_MARGIN : -BTN_MARGIN);
}

static lv_obj_t* makeButton(lv_obj_t* parent, lv_event_cb_t cb, lv_obj_t** out_label) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, 74, 62);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x606060), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, &ui_font_roboto_bold_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0xC0C0C0), LV_PART_MAIN);
    lv_obj_center(label);
    *out_label = label;
    return btn;
}

void HudScreenController::buildUi() {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    // Speed — the point of the whole device. Centred on the screen, deliberately
    // ignoring the buttons: they overlay it and spend most of their time hidden,
    // so letting them shift the number would leave it off-centre in the state
    // the display is normally in.
    speed_label_ = lv_label_create(scr);
    lv_obj_set_style_text_font(speed_label_, &ui_font_roboto_bold_200, LV_PART_MAIN);

    mode_btn_   = makeButton(scr, onModeButton,       &mode_btn_label_);
    bright_btn_ = makeButton(scr, onBrightnessButton, &bright_btn_label_);

    positionButtons();
    refreshModeButton();
    refreshBrightnessButton();
    repaint();  // establishes the "--" offline placeholder and centres the label
}

void HudScreenController::refreshModeButton() {
    // Shows the mode you'd switch *to*, so the button reads as an action.
    lv_label_set_text(mode_btn_label_, mode_ == Mode::SIMPLE ? "HUD" : "SCR");
}

void HudScreenController::refreshBrightnessButton() {
    lv_label_set_text_fmt(bright_btn_label_, "%u%%", brightness_.getCurrentPercent());
}

// Physical layout, identical in both modes: mode button top-right, brightness
// button bottom-right, as seen by the driver.
void HudScreenController::positionButtons() {
    const bool flip_x = (mode_ == Mode::HUD) && MIRROR_HORIZONTAL;
    const bool flip_y = (mode_ == Mode::HUD) && MIRROR_VERTICAL;

    alignPhysicalCorner(mode_btn_,   /*top=*/true,  /*right=*/true, flip_x, flip_y);
    alignPhysicalCorner(bright_btn_, /*top=*/false, /*right=*/true, flip_x, flip_y);
}

void HudScreenController::setButtonsVisible(bool visible) {
    if (buttons_visible_ == visible) return;
    buttons_visible_ = visible;

    // LVGL invalidates the area itself, so the speed underneath repaints.
    if (visible) {
        lv_obj_clear_flag(mode_btn_,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(bright_btn_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(mode_btn_,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(bright_btn_, LV_OBJ_FLAG_HIDDEN);
    }
}

void HudScreenController::setMode(Mode mode) {
    if (mode_ == mode) return;
    mode_ = mode;
    refreshModeButton();
    positionButtons();  // re-anchor so both buttons stay put on the glass

    // The mirror only takes effect as areas are re-flushed, so force a full
    // redraw — otherwise the un-mirrored frame stays on screen until something
    // else happens to invalidate it.
    lv_obj_invalidate(lv_screen_active());
    Serial.printf("[SCREEN] mode -> %s\n", mode_ == Mode::HUD ? "HUD" : "SIMPLE");
}

// ── LVGL callbacks ───────────────────────────────────────────────────────────

uint32_t HudScreenController::tickCb() {
    return millis();
}

void HudScreenController::flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    const int32_t w = lv_area_get_width(area);
    const int32_t h = lv_area_get_height(area);
    uint16_t* src = (uint16_t*)px_map;

    const bool mirror_x = instance_->mode_ == Mode::HUD && MIRROR_HORIZONTAL;
    const bool mirror_y = instance_->mode_ == Mode::HUD && MIRROR_VERTICAL;

    if (!mirror_x && !mirror_y) {
        s_canvas->draw16bitRGBBitmap(area->x1, area->y1, src, w, h);
    } else {
        // Reflect the area's position, then reflect the pixels inside it. Done
        // per row so a partial area lands in the right place on the far side.
        const int32_t dst_x = mirror_x ? (SCREEN_W - 1 - area->x2) : area->x1;
        for (int32_t row = 0; row < h; ++row) {
            uint16_t* s = src + (size_t)row * w;
            uint16_t* out;
            if (mirror_x) {
                for (int32_t i = 0; i < w; ++i) s_mirror_row[i] = s[w - 1 - i];
                out = s_mirror_row;
            } else {
                out = s;
            }
            const int32_t dst_y = mirror_y ? (SCREEN_H - 1 - (area->y1 + row)) : (area->y1 + row);
            s_canvas->draw16bitRGBBitmap(dst_x, dst_y, out, w, 1);
        }
    }

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
            self->wake_only_ = !self->buttons_visible_;
        }

        // LVGL laid the buttons out un-mirrored, but HUD mode paints them
        // mirrored, so a finger on the glass has to be reflected back into
        // LVGL's coordinate space.
        if (self->mode_ == Mode::HUD && MIRROR_HORIZONTAL) x = SCREEN_W - 1 - x;
        if (self->mode_ == Mode::HUD && MIRROR_VERTICAL)   y = SCREEN_H - 1 - y;
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

void HudScreenController::onModeButton(lv_event_t* e) {
    LV_UNUSED(e);
    HudScreenController* self = instance_;
    self->setMode(self->mode_ == Mode::SIMPLE ? Mode::HUD : Mode::SIMPLE);
}

void HudScreenController::onBrightnessButton(lv_event_t* e) {
    LV_UNUSED(e);
    HudScreenController* self = instance_;
    self->brightness_.next();
    self->refreshBrightnessButton();
}

// ── Data in ──────────────────────────────────────────────────────────────────

void HudScreenController::onPayloadReceived(const Payload& payload) {
    // Runs in the ESP-NOW callback: just record, let tick() touch the widgets.
    // LVGL is not reentrant and must only be driven from the loop task.
    speed_kmh_ = payload.speed_kmh;
}

void HudScreenController::onServerStatusChanged(bool online) {
    online_ = online;
}

// Paints shown_speed_ / shown_online_ — the snapshot tick() already took, never
// the volatile fields, so the text and the colour can't disagree.
void HudScreenController::repaint() {
    if (shown_online_) {
        lv_label_set_text_fmt(speed_label_, "%u", shown_speed_);
        lv_obj_set_style_text_color(speed_label_, lv_color_white(), LV_PART_MAIN);
    } else {
        // No server: dashes in grey, so a stale reading is never mistaken for live.
        lv_label_set_text(speed_label_, "--");
        lv_obj_set_style_text_color(speed_label_, lv_color_hex(0x505050), LV_PART_MAIN);
    }
    // Re-centre after every text change: the label auto-sizes, so "9" and "120"
    // would otherwise sit at different offsets.
    lv_obj_align(speed_label_, LV_ALIGN_CENTER, 0, 0);
}

void HudScreenController::tick() {
    // Snapshot once: the ESP-NOW callback can land between the two reads.
    const uint8_t speed  = speed_kmh_;
    const bool    online = online_;
    if (speed != shown_speed_ || online != shown_online_) {
        shown_speed_  = speed;
        shown_online_ = online;
        repaint();
    }

    // Auto-hide the buttons once the screen has been left alone, so normal
    // driving shows nothing but the speed. Evaluated before lv_task_handler()
    // so the visibility LVGL hit-tests against is the one the user can see.
    setButtonsVisible((millis() - last_touch_ms_) < BUTTONS_IDLE_HIDE_MS);

    lv_task_handler();  // may call flushCb, which raises canvas_dirty_

    if (canvas_dirty_) {
        s_canvas->flush();
        canvas_dirty_ = false;
    }
}

#endif // UNIT_TEST
