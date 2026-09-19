#include "hud_screen_controller.h"

#ifndef UNIT_TEST

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>

#include "pin_config.h"

// Roboto Bold, generated from ui/client_simple_hud/assets/Roboto-Bold.ttf by
// lv_font_conv (see CLAUDE.md). The 150 px face carries digits and '-' only, so
// its line height (110 px) is the digit box itself — no ascent padding above.
// Three digits advance 258 px, inside SCREEN_W with room to spare.
extern const lv_font_t ui_font_roboto_bold_150;
extern const lv_font_t ui_font_roboto_bold_28;

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
static constexpr int16_t BTN_W      = 74;
static constexpr int16_t BTN_H      = 62;

// Gap above the digits. The face has no ascent padding, so this is the whole
// visual margin — drop it to 0 to sit the number flush against the top edge.
static constexpr int16_t SPEED_TOP_MARGIN = 16;

static lv_obj_t* makeButton(lv_obj_t* parent, const char* text, lv_event_cb_t cb) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, BTN_W, BTN_H);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x606060), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &ui_font_roboto_bold_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0xC0C0C0), LV_PART_MAIN);
    lv_obj_center(label);
    return btn;
}

void HudScreenController::buildUi() {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    // Speed — the point of the whole device. Sits at the top; the brightness row
    // owns the bottom, so the two never overlap and the number never shifts.
    speed_label_ = lv_label_create(scr);
    lv_obj_set_style_text_font(speed_label_, &ui_font_roboto_bold_150, LV_PART_MAIN);

    // Brightness row along the bottom edge: '-' left, current percent centred,
    // '+' right. The buttons take opposite corners so a blind tap on a moving
    // vehicle cannot hit the wrong one.
    bright_down_btn_ = makeButton(scr, "-", onBrightnessDown);
    bright_up_btn_   = makeButton(scr, "+", onBrightnessUp);
    lv_obj_align(bright_down_btn_, LV_ALIGN_BOTTOM_LEFT,   BTN_MARGIN, -BTN_MARGIN);
    lv_obj_align(bright_up_btn_,   LV_ALIGN_BOTTOM_RIGHT, -BTN_MARGIN, -BTN_MARGIN);

    bright_pct_label_ = lv_label_create(scr);
    lv_obj_set_style_text_font(bright_pct_label_, &ui_font_roboto_bold_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(bright_pct_label_, lv_color_hex(0xC0C0C0), LV_PART_MAIN);

    refreshBrightnessLabel();
    repaint();  // establishes the "--" offline placeholder and places the label
}

void HudScreenController::refreshBrightnessLabel() {
    lv_label_set_text_fmt(bright_pct_label_, "%u%%", brightness_.getCurrentPercent());

    // Centre the label on the button row: LVGL anchors a BOTTOM_MID object by its
    // bottom edge, so lift it by half its own height to line the text's middle up
    // with the buttons' middle. Re-applied on every change — the label auto-sizes,
    // and LVGL does not re-run the alignment itself, so "10%" and "100%" would
    // otherwise drift sideways.
    const int16_t half_text = (int16_t)(lv_font_get_line_height(&ui_font_roboto_bold_28) / 2);
    lv_obj_align(bright_pct_label_, LV_ALIGN_BOTTOM_MID, 0,
                 half_text - (BTN_MARGIN + BTN_H / 2));
}

void HudScreenController::setButtonsVisible(bool visible) {
    if (buttons_visible_ == visible) return;
    buttons_visible_ = visible;

    // LVGL invalidates the area itself, so the speed underneath repaints.
    if (visible) {
        lv_obj_clear_flag(bright_up_btn_,    LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(bright_down_btn_,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(bright_pct_label_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(bright_up_btn_,    LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(bright_down_btn_,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(bright_pct_label_, LV_OBJ_FLAG_HIDDEN);
    }
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
            self->wake_only_ = !self->buttons_visible_;
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

void HudScreenController::onBrightnessUp(lv_event_t* e) {
    LV_UNUSED(e);
    HudScreenController* self = instance_;
    self->brightness_.increase();
    self->refreshBrightnessLabel();
}

void HudScreenController::onBrightnessDown(lv_event_t* e) {
    LV_UNUSED(e);
    HudScreenController* self = instance_;
    self->brightness_.decrease();
    self->refreshBrightnessLabel();
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
    // Re-align after every text change: the label auto-sizes, so "9" and "120"
    // would otherwise sit at different offsets.
    lv_obj_align(speed_label_, LV_ALIGN_TOP_MID, 0, SPEED_TOP_MARGIN);
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
