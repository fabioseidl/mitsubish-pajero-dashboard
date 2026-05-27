#include "debug_screen.h"

#include <Arduino.h>
#include <Wire.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <string.h>

// All diagnostics use ESP_LOGx() which outputs on UART0 — visible on the
// JTAG/UART port regardless of the USB CDC Serial connection status.
static const char* TAG_DS = "DebugScreen";

// LovyanGFX — ESP32-S3 RGB parallel + GT911 touch
// NOTE: ESP32_Display_Panel v0.1.6 fails to build with espressif32@6.9.0 due
// to a C++ aggregate-init incompatibility in ledc_timer_config_t.  LovyanGFX
// (^1.2.0) provides the same functionality and is already proven in this repo.
#include <LovyanGFX.hpp>
// Panel_RGB and Bus_RGB are ESP32-S3-specific and are NOT pulled in by
// <LovyanGFX.hpp> automatically — they must be included explicitly.
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

#include "pin_config.h"
#include "display_config.h"
#include "payload.h"

// ===========================================================================
// LovyanGFX board class — ESP32-S3 + ST7262 RGB + GT911 touch
// ===========================================================================

class LGFX_Debug : public lgfx::LGFX_Device {
    lgfx::Panel_RGB   _panel_instance;
    lgfx::Bus_RGB     _bus_instance;
    lgfx::Touch_GT911 _touch_instance;

public:
    LGFX_Debug() {
        // ---- RGB bus ---------------------------------------------------
        {
            auto cfg = _bus_instance.config();
            cfg.panel = &_panel_instance;

            // Data lines: 16-bit RGB565 = R[4:0] | G[5:0] | B[4:0]
            // Pin assignments from the board GPIO table (pin_config.h).
            // Standard RGB565 bit layout on data bus:
            //   D15..D11 = R4..R0  (red MSB→LSB)
            //   D10..D5  = G5..G0  (green MSB→LSB)
            //   D4..D0   = B4..B0  (blue MSB→LSB)
            //
            // The board uses the controller's channel name ("R3"=R0, "R7"=R4
            // etc. with the channel number indicating the absolute bit position in
            // an 8-bit channel).  Mapping:
            //   R7(GPIO40)=D15, R6(41)=D14, R5(42)=D13, R4(2)=D12, R3(1)=D11
            //   G7(21)=D10, G6(47)=D9, G5(48)=D8, G4(45)=D7, G3(0)=D6, G2(39)=D5
            //   B7(10)=D4,  B6(17)=D3, B5(18)=D2, B4(38)=D1, B3(14)=D0
            //
            // The supplier's GPIO table omits G5 (GPIO48) and G6 (GPIO47).
            // These are confirmed NOT wired to the LCD panel on this board —
            // those two green bits are always 0. GPIO48/47 must still be assigned
            // here as valid numbers because LovyanGFX's Bus_RGB creates a dummy
            // I80 bus internally (to access the LCD peripheral GPIO matrix) and
            // that call fails hard if any data pin is -1.  The two GPIOs will be
            // driven by the LCD peripheral but their outputs go nowhere.
            cfg.pin_d0  = PIN_LCD_B3;   // 14  — B0 on bus (blue LSB)
            cfg.pin_d1  = PIN_LCD_B4;   // 38
            cfg.pin_d2  = PIN_LCD_B5;   // 18
            cfg.pin_d3  = PIN_LCD_B6;   // 17
            cfg.pin_d4  = PIN_LCD_B7;   // 10  — B4 on bus (blue MSB)
            cfg.pin_d5  = PIN_LCD_G2;   // 39  — G0 on bus (green LSB, =G2 on panel)
            cfg.pin_d6  = PIN_LCD_G3;   // 0
            cfg.pin_d7  = PIN_LCD_G4;   // 45
            cfg.pin_d8  = PIN_LCD_G5;   // 48  — NOT wired on panel; dummy valid GPIO
            cfg.pin_d9  = PIN_LCD_G6;   // 47  — NOT wired on panel; dummy valid GPIO
            cfg.pin_d10 = PIN_LCD_G7;   // 21  — G5 on bus (green MSB, =G7 on panel)
            cfg.pin_d11 = PIN_LCD_R3;   // 1   — R0 on bus (red LSB)
            cfg.pin_d12 = PIN_LCD_R4;   // 2
            cfg.pin_d13 = PIN_LCD_R5;   // 42
            cfg.pin_d14 = PIN_LCD_R6;   // 41
            cfg.pin_d15 = PIN_LCD_R7;   // 40  — R4 on bus (red MSB)

            cfg.pin_henable = PIN_LCD_DE;
            cfg.pin_vsync   = PIN_LCD_VSYNC;
            cfg.pin_hsync   = PIN_LCD_HSYNC;
            cfg.pin_pclk    = PIN_LCD_PCLK;

            // Pixel clock — 16 MHz conservative/stable start (up to ~30 MHz if stable).
            cfg.freq_write        = 16000000;

            // Sync timings verified working per BOARD_SPEC.md §2.1.
            // NOTE: LovyanGFX Bus_RGB stores these as int8_t (max 127).
            // Do NOT use the ESPHome YAML values (hpw=162, hbp=152) — they overflow.
            cfg.hsync_polarity    = 0;  // active-low
            cfg.hsync_front_porch = 40;
            cfg.hsync_pulse_width = 48;
            cfg.hsync_back_porch  = 88;

            cfg.vsync_polarity    = 0;  // active-low
            cfg.vsync_front_porch = 3;
            cfg.vsync_pulse_width = 10;
            cfg.vsync_back_porch  = 18;

            // PCLK idles LOW; data latched on falling edge.
            cfg.pclk_idle_high    = 0;

            _bus_instance.config(cfg);
        }

        // ---- Panel -----------------------------------------------------
        {
            auto cfg = _panel_instance.config();
            cfg.memory_width  = LCD_H_RES;
            cfg.memory_height = LCD_V_RES;
            cfg.panel_width   = LCD_H_RES;
            cfg.panel_height  = LCD_V_RES;
            cfg.offset_x      = 0;
            cfg.offset_y      = 0;
            _panel_instance.config(cfg);
        }

        // ---- Panel detail — allocate frame buffer from PSRAM -----------
        {
            auto cfg = _panel_instance.config_detail();
            cfg.use_psram = 2;  // both framebuffers in PSRAM → tear-free DMA
            _panel_instance.config_detail(cfg);
        }

        // ---- GT911 touch (I2C) -----------------------------------------
        // NOTE: LovyanGFX calls Wire.begin(sda, scl) here during s_lgfx->init().
        // Wire is already initialised in DebugScreen::begin() before this runs,
        // so the call is a no-op and produces a benign Arduino warning.  Both
        // the I2C expander and the touch controller use the same bus (port 0,
        // SDA=8, SCL=9) so there is no functional conflict.
        {
            auto cfg = _touch_instance.config();
            cfg.x_min           = 0;
            cfg.x_max           = LCD_H_RES - 1;
            cfg.y_min           = 0;
            cfg.y_max           = LCD_V_RES - 1;
            cfg.pin_int         = PIN_TOUCH_INT;
            cfg.bus_shared      = false;
            cfg.offset_rotation = 0;
            cfg.i2c_port        = 0;
            cfg.i2c_addr        = 0x5D;  // GT911 default; alt = 0x14
            cfg.pin_sda         = PIN_I2C_SDA;
            cfg.pin_scl         = PIN_I2C_SCL;
            cfg.freq            = 400000;
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }

        _panel_instance.setBus(&_bus_instance);
        setPanel(&_panel_instance);
    }
};

// ---------------------------------------------------------------------------
// File-scope display object (avoids static constructor ordering issues)
// ---------------------------------------------------------------------------
static LGFX_Debug* s_lgfx = nullptr;

// LVGL draw buffers — allocated from PSRAM in initLvgl()
static lv_color_t* s_lvgl_buf1 = nullptr;
static lv_color_t* s_lvgl_buf2 = nullptr;

// ===========================================================================
// I2C expander helpers — PCF8574 at IO_EXP_ADDR (0x24)
//
// PCF8574 protocol: single-byte write drives all 8 output pins directly.
// There are no register addresses — just one byte per transaction.
// ===========================================================================

static uint8_t s_exp_out = 0x00;  // shadow of current output state

static void expWrite(uint8_t val) {
    Wire.beginTransmission(IO_EXP_ADDR);
    Wire.write(val);
    Wire.endTransmission();
}

static void expInit() {
    s_exp_out = 0x00;
    expWrite(s_exp_out);  // all outputs LOW (resets asserted, backlight off)
}

static void expSetPin(uint8_t pin, bool level) {
    if (level) s_exp_out |=  (1u << pin);
    else       s_exp_out &= ~(1u << pin);
    expWrite(s_exp_out);
}

// ===========================================================================
// LVGL callbacks
// ===========================================================================

static void lvgl_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area,
                          lv_color_t* color_p) {
    if (s_lgfx) {
        // Write this strip into the LovyanGFX back buffer.
        // pushImage does NOT trigger an endTransaction/buffer-swap on its own,
        // so multiple strip calls are safe before the swap.
        s_lgfx->pushImage(area->x1, area->y1,
                          area->x2 - area->x1 + 1,
                          area->y2 - area->y1 + 1,
                          (lgfx::rgb565_t*)color_p);

        // Swap back→front only when the last strip of the frame is done.
        // This ensures the LCD always sees a complete, coherent frame — no
        // partially-rendered state is ever visible.
        if (lv_disp_flush_is_last(drv)) {
            s_lgfx->display();
        }
    }
    lv_disp_flush_ready(drv);
}

static void lvgl_touch_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    if (!s_lgfx) { data->state = LV_INDEV_STATE_REL; return; }
    uint16_t tx = 0, ty = 0;
    bool pressed = s_lgfx->getTouch(&tx, &ty);
    data->state   = pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    data->point.x = (lv_coord_t)tx;
    data->point.y = (lv_coord_t)ty;
}

// ===========================================================================
// PID descriptor table
// ===========================================================================

#define PD_F1(lbl, unit, fld) \
    { lbl, unit, PidType::FLOAT1,    (uint16_t)offsetof(Payload, fld) }
#define PD_F2(lbl, unit, fld) \
    { lbl, unit, PidType::FLOAT2,    (uint16_t)offsetof(Payload, fld) }
#define PD_U(lbl, unit, fld) \
    { lbl, unit, PidType::UINT_DEC,  (uint16_t)offsetof(Payload, fld) }
#define PD_B(lbl, fld) \
    { lbl, "",   PidType::BOOL_YESNO,(uint16_t)offsetof(Payload, fld) }

static const PidDescriptor k_pids[] = {
    // ---- Core ----
    PD_U ("Version",          "",       version),
    PD_U ("Timestamp",        "ms",     timestamp_ms),
    PD_U ("RPM",              "rpm",    rpm),
    PD_U ("Speed",            "km/h",   speed_kmh),
    PD_F1("Fuel Rate",        "L/h",    fuel_rate_l_per_h),
    PD_F1("Consumption",      "km/L",   consumption_km_per_l),
    PD_F1("Avg Consumption",  "km/L",   avg_consumption_km_per_l),
    PD_F1("Distance",         "km",     distance_km),
    PD_B ("MIL On",                     mil_on),
    PD_U ("DTC Count",        "",       dtc_count),
    // ---- Verified Mode 01 ----
    PD_F1("Engine Load",      "%",      engine_load_pct),
    PD_F1("Coolant Temp",     "C",      coolant_temp_c),
    PD_U ("MAP Pressure",     "kPa",    map_pressure_kpa),
    PD_F1("Intake Air Temp",  "C",      intake_air_temp_c),
    PD_F2("MAF",              "g/s",    maf_g_per_s),
    PD_F1("Throttle",         "%",      throttle_pct),
    PD_U ("Runtime",          "s",      runtime_s),
    PD_U ("Dist MIL",         "km",     dist_mil_km),
    PD_F1("Fuel Rail Pres",   "kPa",    fuel_rail_pres_kpa),
    PD_F1("Cmd EGR",          "%",      egr_cmd_pct),
    PD_F1("EGR Error",        "%",      egr_error_pct),
    PD_U ("Warmups",          "",       warmups),
    PD_U ("Dist Cleared",     "km",     dist_cleared_km),
    PD_U ("Baro Pressure",    "kPa",    baro_pressure_kpa),
    PD_F1("Catalyst Temp",    "C",      catalyst_temp_c),
    PD_F2("ECU Voltage",      "V",      module_voltage_v),
    PD_F1("Rel Throttle",     "%",      rel_throttle_pct),
    PD_F1("Accel Pedal D",    "%",      accel_d_pct),
    PD_F1("Accel Pedal E",    "%",      accel_e_pct),
    PD_F1("Throttle Act",     "%",      throttle_act_pct),
    PD_U ("Time MIL",         "min",    time_mil_min),
    PD_U ("Time Cleared",     "min",    time_cleared_min),
    PD_U ("OBD Standards",    "",       obd_standards),
    // ---- Unverified Mode 01 ----
    PD_F1("STFT B1",          "%",      stft_pct),
    PD_F1("LTFT B1",          "%",      ltft_pct),
    PD_F1("Fuel Pressure",    "kPa",    fuel_pressure_kpa),
    PD_F2("O2 Sensor",        "",       o2_sensor),
    PD_F1("Abs Load",         "%",      abs_load_pct),
    PD_F2("Cmd AFR",          "lam",    cmd_afr_lambda),
    PD_F1("Ambient Temp",     "C",      ambient_temp_c),
    PD_F1("Throttle B",       "%",      throttle_b_pct),
    PD_F1("Hybrid Batt",      "%",      hybrid_batt_pct),
    PD_F1("Oil Temp",         "C",      oil_temp_c),
    // ---- Mode 22 — AT ECU ----
    PD_F1("AT Gear Pos",      "",       at_gear_pos),
    PD_F2("AT Gear Ratio",    "",       at_gear_ratio),
    PD_F1("AT Input Speed",   "rpm",    at_input_speed_rpm),
    PD_F1("AT Output Speed",  "rpm",    at_output_speed_rpm),
    PD_F1("AT TC Slip",       "rpm",    at_tc_slip_rpm),
    PD_F1("ATF Temp",         "C",      at_atf_temp_c),
    PD_F1("Shift Sol",        "",       at_shift_sol_status),
    PD_F1("Lockup Status",    "",       at_lockup_status),
    PD_F1("PRNDL",            "",       at_prndl),
    PD_F1("Target Gear",      "",       at_target_gear),
    PD_F1("AT Oil Pres",      "",       at_oil_pres),
    // ---- Mode 22 — Engine ECU ----
    PD_F1("Boost Pres",       "kPa",    boost_pres),
    PD_F1("EGR Valve Pos",    "%",      egr_valve_pos_pct),
    PD_F1("DPF Soot",         "",       dpf_soot_load),
    PD_F1("DPF Regen",        "",       dpf_regen_status),
    PD_F1("Rail Pres Act",    "kPa",    rail_pres_act),
    PD_F1("Rail Pres Des",    "kPa",    rail_pres_des),
    PD_F1("Inj Corr Cyl1",   "%",      inj_cor_cyl1),
    PD_F1("Inj Corr Cyl2",   "%",      inj_cor_cyl2),
    PD_F1("Inj Corr Cyl3",   "%",      inj_cor_cyl3),
    PD_F1("Inj Corr Cyl4",   "%",      inj_cor_cyl4),
    // ---- Flags ----
    PD_U ("Flags",            "",       flags),
};

static constexpr int k_pid_count = (int)(sizeof(k_pids) / sizeof(k_pids[0]));

#undef PD_F1
#undef PD_F2
#undef PD_U
#undef PD_B

// ===========================================================================
// Hardware init helpers
// ===========================================================================

bool DebugScreen::initExpander() {
    expInit();
    // Assert all resets, backlight off
    expSetPin(IO_EXP_LCD_RST,   false);
    expSetPin(IO_EXP_TOUCH_RST, false);
    expSetPin(IO_EXP_BACKLIGHT, false);
    expSetPin(IO_EXP_SD_CS,     true);   // SD deselected
    delay(10);
    return true;
}

bool DebugScreen::initPanel() {
    // Release BOTH resets together before calling s_lgfx->init().
    //
    // IMPORTANT: LovyanGFX initialises the GT911 touch controller *inside*
    // Panel_Device::init() — that is, during s_lgfx->init() below.  If
    // IO_EXP_TOUCH_RST is still asserted (LOW) when LGFX tries to talk to
    // the GT911, the I2C slave holds SDA low and Wire::endTransmission()
    // blocks forever.  The separate initTouch() helper is now a no-op but
    // is kept in the call chain for symmetry.
    expSetPin(IO_EXP_LCD_RST,   true);  // ST7262: deassert reset
    expSetPin(IO_EXP_TOUCH_RST, true);  // GT911:  deassert reset
    delay(60);  // GT911 requires ≥50 ms after reset before I2C is ready;
                // 60 ms also covers the ST7262's 20 ms requirement.

    s_lgfx = new LGFX_Debug();
    if (!s_lgfx) {
        ESP_LOGE(TAG_DS, "LGFX alloc failed");
        return false;
    }
    // NOTE: s_lgfx->init() always returns true (Panel_Device::init ignores
    // bus init return value internally).  We still check for future safety.
    if (!s_lgfx->init()) {
        ESP_LOGE(TAG_DS, "LGFX init returned false — check Bus_RGB config");
        delete s_lgfx;
        s_lgfx = nullptr;
        return false;
    }
    ESP_LOGI(TAG_DS, "LGFX init OK — filling screen white for HW test");
    // ---- Bare-metal screen-fill test (no LVGL, no flush callback) ----------
    // Writes white pixels directly into the LovyanGFX frame buffer.
    // If the display hardware is working you will see a white screen before
    // the LVGL UI appears.  If the screen stays dark: backlight or panel issue.
    s_lgfx->fillScreen(TFT_WHITE);
    delay(500);
    s_lgfx->fillScreen(TFT_RED);
    delay(500);
    s_lgfx->fillScreen(TFT_BLACK);
    return true;
}

bool DebugScreen::initTouch() {
    // Reset is released in initPanel() before s_lgfx->init() (see comment
    // there).  Nothing left to do here; the function is kept for symmetry.
    return true;
}

void DebugScreen::initLvgl() {
    lv_init();

    // Prefer internal DRAM: zero PSRAM bus contention with the LCD DMA.
    // At 60 lines × 1024 × 2 bytes = 122 KB per buffer, two buffers = ~245 KB —
    // fits in ESP32-S3 internal SRAM when WiFi is active.
    s_lvgl_buf1 = (lv_color_t*)heap_caps_malloc(
        LVGL_BUF_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_lvgl_buf2 = (lv_color_t*)heap_caps_malloc(
        LVGL_BUF_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    bool in_psram = false;

    if (!s_lvgl_buf1 || !s_lvgl_buf2) {
        ESP_LOGW(TAG_DS, "DRAM alloc failed, falling back to PSRAM");
        free(s_lvgl_buf1); free(s_lvgl_buf2);
        s_lvgl_buf1 = (lv_color_t*)heap_caps_malloc(
            LVGL_BUF_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        s_lvgl_buf2 = (lv_color_t*)heap_caps_malloc(
            LVGL_BUF_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        in_psram = true;
        if (!s_lvgl_buf1 || !s_lvgl_buf2) {
            ESP_LOGE(TAG_DS, "FATAL: no LVGL draw buffer memory");
            return;
        }
    }
    ESP_LOGI(TAG_DS, "LVGL buffers OK (%u bytes each in %s)",
             LVGL_BUF_BYTES, in_psram ? "PSRAM" : "DRAM");

    // LVGL 8.x display driver
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, s_lvgl_buf1, s_lvgl_buf2, LVGL_BUF_PIXELS);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = (lv_coord_t)LCD_H_RES;
    disp_drv.ver_res      = (lv_coord_t)LCD_V_RES;
    disp_drv.draw_buf     = &draw_buf;
    disp_drv.flush_cb     = lvgl_flush_cb;
    // full_refresh = 1: LVGL always renders the complete screen before calling
    // flush. Combined with the display() swap at the last strip, the LCD only
    // ever shows a fully-composed frame — eliminates all strip-by-strip shaking.
    disp_drv.full_refresh = 1;
    lv_disp_drv_register(&disp_drv);

    // LVGL 8.x touch input driver
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_cb;
    lv_indev_drv_register(&indev_drv);
}

// ===========================================================================
// IScreenController::begin()
// ===========================================================================

bool DebugScreen::begin() {
    // Initialise I2C once here so both the PCF8574 expander (used immediately
    // in initExpander) and LovyanGFX's Touch_GT911 (initialised later inside
    // s_lgfx->init()) share the same already-open bus without triggering
    // Arduino Wire's "bus already initialized" error.
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    ESP_LOGI(TAG_DS, "I2C ready (SDA=%d SCL=%d)", PIN_I2C_SDA, PIN_I2C_SCL);

    if (!initExpander()) { ESP_LOGE(TAG_DS, "initExpander FAILED"); return false; }
    ESP_LOGI(TAG_DS, "expander OK");

    if (!initPanel())    { ESP_LOGE(TAG_DS, "initPanel FAILED");    return false; }
    ESP_LOGI(TAG_DS, "panel OK");

    if (!initTouch())    { ESP_LOGE(TAG_DS, "initTouch FAILED");    return false; }
    ESP_LOGI(TAG_DS, "touch OK");

    expSetPin(IO_EXP_BACKLIGHT, true);  // backlight on after display is running
    ESP_LOGI(TAG_DS, "backlight ON");

    initLvgl();
    ESP_LOGI(TAG_DS, "LVGL ready");

    buildUi();

    last_tick_ms_ = (uint32_t)(esp_timer_get_time() / 1000);
    ESP_LOGI(TAG_DS, "init complete");
    return true;
}

// ===========================================================================
// LVGL UI construction
// ===========================================================================

void DebugScreen::buildUi() {
    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x111111), LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ---- Header bar -------------------------------------------------------
    lv_obj_t* header = lv_obj_create(scr);
    lv_obj_set_size(header, LCD_H_RES, HEADER_H);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x1a1a2e), LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(header, 4, LV_PART_MAIN);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "PAJERO DEBUG - ALL PIDs");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xe0e0e0), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 4, 0);

    lbl_status_ = lv_label_create(header);
    lv_label_set_text(lbl_status_, "WAITING...");
    lv_obj_set_style_text_color(lbl_status_, lv_color_hex(0xaaaaaa), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_status_, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(lbl_status_, LV_ALIGN_CENTER, 0, 0);

    lbl_ts_ = lv_label_create(header);
    lv_label_set_text(lbl_ts_, "---");
    lv_obj_set_style_text_color(lbl_ts_, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_ts_, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_align(lbl_ts_, LV_ALIGN_RIGHT_MID, -4, 0);

    // ---- Scrollable body --------------------------------------------------
    lv_obj_t* body = lv_obj_create(scr);
    lv_obj_set_size(body, LCD_H_RES, BODY_H);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, HEADER_H);
    lv_obj_set_style_bg_color(body, lv_color_hex(0x111111), LV_PART_MAIN);
    lv_obj_set_style_border_width(body, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(body, 0, LV_PART_MAIN);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);

    // ---- PID table — 6 columns, ceil(k_pid_count/2) rows -----------------
    int half_count = (k_pid_count + 1) / 2;

    table_ = lv_table_create(body);
    lv_table_set_col_cnt(table_, TABLE_COL_COUNT);
    lv_table_set_row_cnt(table_, half_count);

    lv_table_set_col_width(table_, 0, TABLE_COL_W_LABEL);
    lv_table_set_col_width(table_, 1, TABLE_COL_W_VALUE);
    lv_table_set_col_width(table_, 2, TABLE_COL_W_UNIT);
    lv_table_set_col_width(table_, 3, TABLE_COL_W_LABEL);
    lv_table_set_col_width(table_, 4, TABLE_COL_W_VALUE);
    lv_table_set_col_width(table_, 5, TABLE_COL_W_UNIT);

    lv_obj_set_style_text_font(table_, &lv_font_montserrat_12, LV_PART_ITEMS);
    lv_obj_set_style_pad_top(table_,    2, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(table_, 2, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(table_,   4, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(table_,  4, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(table_, lv_color_hex(0x111111), LV_PART_ITEMS);
    lv_obj_set_style_text_color(table_, lv_color_hex(0xcccccc), LV_PART_ITEMS);
    lv_obj_set_style_border_color(table_, lv_color_hex(0x333333), LV_PART_ITEMS);

    for (int i = 0; i < k_pid_count; i++) {
        int row      = i % half_count;
        int col_base = (i < half_count) ? 0 : 3;

        lv_table_set_cell_value(table_, row, col_base + 0, k_pids[i].label);
        lv_table_set_cell_value(table_, row, col_base + 1, "---");
        lv_table_set_cell_value(table_, row, col_base + 2, k_pids[i].unit);
    }

    lv_obj_align(table_, LV_ALIGN_TOP_LEFT, 0, 0);
}

// ===========================================================================
// WiFi-task-safe callbacks — NO LVGL calls here
// ===========================================================================

void DebugScreen::onPayloadReceived(const Payload& payload) {
    portENTER_CRITICAL(&mux_);
    pending_payload_     = payload;
    has_pending_payload_ = true;
    portEXIT_CRITICAL(&mux_);
}

void DebugScreen::onServerStatusChanged(bool online) {
    portENTER_CRITICAL(&mux_);
    pending_status_     = online;
    has_pending_status_ = true;
    portEXIT_CRITICAL(&mux_);
}

// ===========================================================================
// Loop-task: tick()
// ===========================================================================

void DebugScreen::tick() {
    uint32_t now_ms   = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t delta_ms = now_ms - last_tick_ms_;
    if (delta_ms == 0) delta_ms = 1;
    last_tick_ms_ = now_ms;

    portENTER_CRITICAL(&mux_);
    bool    apply_pl = has_pending_payload_;
    Payload pl_copy  = pending_payload_;
    has_pending_payload_ = false;
    bool apply_st = has_pending_status_;
    bool st_copy  = pending_status_;
    has_pending_status_ = false;
    portEXIT_CRITICAL(&mux_);

    if (apply_st) applyServerStatus(st_copy);
    if (apply_pl) applyPayload(pl_copy);

    lv_tick_inc(delta_ms);
    lv_timer_handler();
}

// ===========================================================================
// applyServerStatus
// ===========================================================================

void DebugScreen::applyServerStatus(bool online) {
    server_online_ = online;
    if (!lbl_status_) return;
    if (online) {
        lv_label_set_text(lbl_status_, "SERVER: ONLINE");
        lv_obj_set_style_text_color(lbl_status_, lv_color_hex(0x00e676), LV_PART_MAIN);
    } else {
        lv_label_set_text(lbl_status_, "SERVER: OFFLINE");
        lv_obj_set_style_text_color(lbl_status_, lv_color_hex(0xff1744), LV_PART_MAIN);
    }
}

// ===========================================================================
// applyPayload — update all PID value cells via the descriptor table
// ===========================================================================

void DebugScreen::applyPayload(const Payload& p) {
    if (lbl_ts_) {
        char ts_buf[24];
        snprintf(ts_buf, sizeof(ts_buf), "%u ms", p.timestamp_ms);
        lv_label_set_text(lbl_ts_, ts_buf);
    }
    if (!table_) return;

    int  half_count = (k_pid_count + 1) / 2;
    char val_buf[24];

    for (int i = 0; i < k_pid_count; i++) {
        int row      = i % half_count;
        int col_base = (i < half_count) ? 0 : 3;
        formatValue(val_buf, sizeof(val_buf), k_pids[i], p);
        lv_table_set_cell_value(table_, row, col_base + 1, val_buf);
    }
}

// ===========================================================================
// formatValue — memcpy-safe read from packed Payload
// ===========================================================================

/*static*/ void DebugScreen::formatValue(char* buf, size_t bufsz,
                                          const PidDescriptor& desc,
                                          const Payload& p) {
    const uint8_t* base = reinterpret_cast<const uint8_t*>(&p);
    switch (desc.type) {
        case PidType::FLOAT1: {
            float v = 0.0f; memcpy(&v, base + desc.offset, 4);
            snprintf(buf, bufsz, "%.1f", v); break;
        }
        case PidType::FLOAT2: {
            float v = 0.0f; memcpy(&v, base + desc.offset, 4);
            snprintf(buf, bufsz, "%.2f", v); break;
        }
        case PidType::UINT_DEC: {
            uint32_t v = 0; memcpy(&v, base + desc.offset, 4);
            snprintf(buf, bufsz, "%u", v); break;
        }
        case PidType::BOOL_YESNO: {
            uint8_t v = 0; memcpy(&v, base + desc.offset, 1);
            snprintf(buf, bufsz, "%s", v ? "YES" : "NO"); break;
        }
        default:
            snprintf(buf, bufsz, "?"); break;
    }
}
