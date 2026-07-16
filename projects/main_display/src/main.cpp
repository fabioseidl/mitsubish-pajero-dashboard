// ============================================================
//  Waveshare ESP32-S3-Touch-LCD-7B — Hello World  (debug v3)
//  Goal: light up the 1024x600 RGB panel and blink the
//        backlight so we know both are working.
// ============================================================

#include <Arduino.h>
#include <Wire.h>

// ── LovyanGFX ─────────────────────────────────────────────────
// Panel_RGB / Bus_RGB are NOT auto-included by device.hpp even
// when CONFIG_IDF_TARGET_ESP32S3 is defined — must be explicit.
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

// ── App: LVGL dashboard + ESP-NOW link to the server ──────────
// The server broadcasts a 221-byte Payload at 10 Hz over ESP-NOW; we render it
// on the 1024x600 panel with a hand-written LVGL dashboard (dashboard_ui.cpp).
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <WiFi.h>

#include "payload.h"
#include "espnow_receiver.h"
#include "server_connection_monitor.h"
#include "security_config.h"   // PMK_KEY (gitignored — created from .example)
#include "app_ui.h"            // SquareLine Studio UI bridge (src/ui/, LVGL 9 export)
#include "gps.h"               // u-blox NEO-6M on UART2 (NMEA → serial console)
#include "gt911.h"             // GT911 capacitive touch on the shared I2C bus
#include "mpu6050.h"           // MPU6050 6-axis IMU on the shared I2C bus (0x68)
#include "aht20_bmp280.h"      // AHT20 (0x38) + BMP280 (0x76/0x77) on the shared I2C bus

// ─────────────────────────────────────────────────────────────
//  Debug macro — routes to UART0 (Serial on this build)
//  All Arduino core [I] messages go to UART0 via the USB-JTAG
//  bridge. Without ARDUINO_USB_CDC_ON_BOOT, Serial = UART0 too,
//  so everything appears on the same USB port.
// ─────────────────────────────────────────────────────────────
#define DBG(...)  do { Serial.printf(__VA_ARGS__); Serial.println(); Serial.flush(); } while(0)

// ─────────────────────────────────────────────────────────────
//  PCF8574 I/O expander  (detected at 0x24 on this board rev)
//  Single I2C address — one-byte write drives all 8 output pins.
//  Pin mapping: TBD by bit-scan diagnostic (see setup()).
//  0x07 (P0|P1|P2) did NOT light the backlight → pino BL é outro.
//  Após a identificação atualizar BL_BIT / TP_BIT / LCD_RST_BIT.
// ─────────────────────────────────────────────────────────────
#define PCF8574_ADDR   0x24

// STALE GUESSES — kept only because nothing reads them any more. They predate
// the bit-scan and disagree with the verified mapping in BOARD_SPEC §5.2
// (P0 = LCD_RST 0x01, P1 = LCD_BL 0x02, P2 = TP_RST 0x04); TP_BIT below is in
// fact the backlight. Use the PCF_* constants underneath instead.
#define LCD_RST_BIT  (1 << 3)   // WRONG — do not use
#define BL_BIT       (1 << 2)   // WRONG — do not use
#define TP_BIT       (1 << 1)   // WRONG — this is the backlight

// Verified mapping (BOARD_SPEC §5.2). Only P1 was ever confirmed empirically by
// the backlight bit-scan; P0/P2 come from the spec text.
static constexpr uint8_t PCF_LCD_RST = 0x01;  // P0
static constexpr uint8_t PCF_LCD_BL  = 0x02;  // P1 — confirmed by bit-scan
static constexpr uint8_t PCF_TP_RST  = 0x04;  // P2

// I2C pins — don't use I2C_SDA / I2C_SCL: esp32s3box/pins_arduino.h
// already defines those as SCL=18, SDA=8, causing a redefinition.
#define DISP_SDA  8
#define DISP_SCL  9

// ─────────────────────────────────────────────────────────────
//  PCF8574 helpers
// ─────────────────────────────────────────────────────────────
static uint8_t g_pcf8574 = 0;

static bool pcf8574_init() {
  Wire.beginTransmission(PCF8574_ADDR);
  uint8_t err = Wire.endTransmission();
  DBG("[GPIO] PCF8574 probe 0x%02X: err=%d %s",
      PCF8574_ADDR, err, err == 0 ? "OK" : "FAIL — check wiring");
  return err == 0;
}

static void pcf8574_write(uint8_t value) {
  g_pcf8574 = value;
  Wire.beginTransmission(PCF8574_ADDR);
  Wire.write(value);          // each bit → one GPIO pin, 1 = HIGH
  uint8_t err = Wire.endTransmission();
  if (err) DBG("[GPIO] pcf8574_write(0x%02X) err=%d", value, err);
  else     DBG("[GPIO] pcf8574_write(0x%02X) OK", value);
}

static void pcf8574_set_bit(uint8_t mask, bool high) {
  if (high) g_pcf8574 |=  mask;
  else      g_pcf8574 &= ~mask;
  pcf8574_write(g_pcf8574);
}

// ─────────────────────────────────────────────────────────────
//  Backlight button — GPIO 6 (the PH2.0 "ADC" sensor connector)
//
//  GPIO 6 is one of the few pins the RGB bus leaves free, and it is broken out
//  on a 3-pin PH2.0 header, so a sensor/button module plugs straight in.
//
//  This is NOT the BOOT button: BOOT is GPIO 0, which on this board is RGB
//  Green 3 (see the LGFX config) and is driven by the LCD peripheral at pixel
//  rate — unreadable while the panel runs, and shorted to GND if pressed.
//
//  Polarity is detected at boot rather than assumed: 3-pin modules differ in
//  whether the switch pulls the signal to GND or to VCC, and some carry their
//  own pull resistor. Whatever level the pin idles at is taken as "released".
//  (So do not hold the button while booting.)
// ─────────────────────────────────────────────────────────────
#define BL_BTN_PIN            6
static constexpr uint32_t BL_BTN_DEBOUNCE_MS = 30;

static int g_btn_pressed_level = LOW;

static void backlight_button_init() {
  pinMode(BL_BTN_PIN, INPUT_PULLUP);
  delay(10);                                   // let the pull settle
  const int idle = digitalRead(BL_BTN_PIN);
  g_btn_pressed_level = (idle == HIGH) ? LOW : HIGH;
  DBG("[button] GPIO%d idles %s → pressed = %s",
      BL_BTN_PIN, idle == HIGH ? "HIGH" : "LOW",
      g_btn_pressed_level == LOW ? "LOW" : "HIGH");
}

static void backlight_button_tick(uint32_t now) {
  static bool     raw_prev    = false;
  static bool     stable      = false;
  static uint32_t last_edge   = 0;

  const bool raw = (digitalRead(BL_BTN_PIN) == g_btn_pressed_level);

  if (raw != raw_prev) {              // bouncing — restart the settle timer
    raw_prev  = raw;
    last_edge = now;
    return;
  }
  if (raw == stable) return;                       // nothing new
  if (now - last_edge < BL_BTN_DEBOUNCE_MS) return; // not settled yet

  stable = raw;
  if (stable) {                        // act on press, ignore release
    app_ui::cycle_backlight();
    DBG("[button] press → backlight step");
  }
}

static void i2c_scan();  // defined below

// ─────────────────────────────────────────────────────────────
//  GT911 bring-up
//
//  The GT911 does not simply answer once reset is released: it samples its INT
//  pin as reset rises to choose its I2C address (INT low → 0x5D, high → 0x14),
//  and stays silent if that never happens properly. BOARD_SPEC §6 lists the INT
//  pin as "project-dependent" and it is not identified for this board, so try
//  the GPIOs the RGB bus and the I2C/UART pins leave free — the same empirical
//  bit-scan approach §5.2 used to find the backlight.
//
//  Runs before lcd.init(), so pulsing PCF_TP_RST is harmless even if the spec's
//  P2 mapping is wrong and the pulse lands on something else.
// ─────────────────────────────────────────────────────────────
// Release TP_RST assuming the expander is a CH422G rather than a PCF8574.
// The two are indistinguishable by an I2C scan (both ACK 0x24) but are driven
// completely differently: a PCF8574 takes the output byte at 0x24, whereas on a
// CH422G 0x24 is the *config* register (bit0 = enable IO outputs) and the output
// byte goes to 0x38. If this board is really a CH422G, every raw 0x24 write we
// do lands in its config register and the IO pins — including TP_RST — are never
// driven, which would explain the GT911 being absent from the bus.
static void ch422g_release_tp_rst(bool high) {
  Wire.beginTransmission(0x24);
  Wire.write(0x01);            // WR_SET: IO_OE — enable the IO outputs
  Wire.endTransmission();
  Wire.beginTransmission(0x38);
  Wire.write(high ? 0xFF : 0x00);  // WR_IO: all outputs high / low
  Wire.endTransmission();
}

static bool gt911_try(int pin, bool use_ch422g) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);              // LOW as reset rises → address 0x5D

  if (use_ch422g) {
    ch422g_release_tp_rst(false);
    delay(20);
    ch422g_release_tp_rst(true);       // release reset, INT still held low
  } else {
    pcf8574_set_bit(PCF_TP_RST, false);
    delay(20);
    pcf8574_set_bit(PCF_TP_RST, true);
  }
  delay(20);
  pinMode(pin, INPUT);                 // hand INT back to the controller
  delay(60);                           // GT911 needs ~50 ms to boot
  return gt911::detect();
}

static bool gt911_bringup() {
  // Free after RGB (0,1,2,3,5,7,10,14,17,18,21,38..48), I2C (8,9), GPS RX (44)
  // and UART0 TX (43). 35/36/37 are the OPI PSRAM pins — never touch those.
  static const int kIntCandidates[] = {4, 6, 15, 16, 11, 12, 13};

  for (int use_ch422g = 0; use_ch422g <= 1; ++use_ch422g) {
    for (int pin : kIntCandidates) {
      if (gt911_try(pin, use_ch422g != 0)) {
        DBG("[touch] GT911 up at 0x%02X — INT=GPIO%d, expander driven as %s",
            gt911::address(), pin, use_ch422g ? "CH422G (0x24 cfg + 0x38 out)"
                                              : "PCF8574 (0x24 out)");
        return true;
      }
    }
  }

  DBG("[touch] GT911 absent: no answer at 0x5D/0x14 for INT in {4,6,15,16,11,12,13}");
  DBG("[touch] tried releasing TP_RST both as PCF8574 and as CH422G — neither worked");
  i2c_scan();

  // Leave the panel lit: the CH422G attempt above may have left the expander in
  // a different state than the 0xFF the rest of the code assumes.
  pcf8574_write(0xFF);
  return false;
}

// ─────────────────────────────────────────────────────────────
//  I2C scanner
// ─────────────────────────────────────────────────────────────
static void i2c_scan() {
  DBG("[I2C] scanning 0x08-0x77 ...");
  int n = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { DBG("[I2C]   found 0x%02X", a); n++; }
  }
  DBG("[I2C] %d device(s)", n);
  // Expected: CH422G @ 0x24 (mode), 0x38 (output); GT911 @ 0x5D
}

// ─────────────────────────────────────────────────────────────
//  LovyanGFX display — public members prevent base-class
//  _panel / _bus shadowing errors.
// ─────────────────────────────────────────────────────────────
class LGFX : public lgfx::LGFX_Device {
public:
  lgfx::Bus_RGB    _bus_instance;
  lgfx::Panel_RGB  _panel_instance;

  LGFX() {
    // ── Panel geometry ────────────────────────────────────────
    {
      auto cfg = _panel_instance.config();
      cfg.memory_width  = 1024;
      cfg.memory_height =  600;
      cfg.panel_width   = 1024;
      cfg.panel_height  =  600;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel_instance.config(cfg);
    }

    // ── Framebuffer in PSRAM ──────────────────────────────────
    // SINGLE framebuffer (use_psram=1) — REQUIRED with LVGL partial redraws.
    // With two framebuffers (use_psram=2) the panel flips between them while
    // LVGL only pushes the *changed* region each frame, so every flip shows a
    // buffer missing the other's recent partial writes → the whole screen
    // flickers. One framebuffer means pushImage always writes the buffer being
    // scanned out: no flip, no flicker (at the cost of mild tearing on a redraw).
    {
      auto cfg = _panel_instance.config_detail();
      cfg.use_psram = 1;   // 1 = single framebuffer in PSRAM
      _panel_instance.config_detail(cfg);
    }

    // ── RGB bus ───────────────────────────────────────────────
    {
      auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;

      // Data pin mapping — Blue on D0–D4, Green on D5–D10, Red on D11–D15.
      cfg.pin_d0  = GPIO_NUM_14;   // Blue  bit 3 (LSB)
      cfg.pin_d1  = GPIO_NUM_38;   // Blue  bit 4
      cfg.pin_d2  = GPIO_NUM_18;   // Blue  bit 5
      cfg.pin_d3  = GPIO_NUM_17;   // Blue  bit 6
      cfg.pin_d4  = GPIO_NUM_10;   // Blue  bit 7 (MSB)
      cfg.pin_d5  = GPIO_NUM_39;   // Green bit 2 (LSB)
      cfg.pin_d6  = GPIO_NUM_0;    // Green bit 3
      cfg.pin_d7  = GPIO_NUM_45;   // Green bit 4
      cfg.pin_d8  = GPIO_NUM_48;   // Green bit 5
      cfg.pin_d9  = GPIO_NUM_47;   // Green bit 6
      cfg.pin_d10 = GPIO_NUM_21;   // Green bit 7 (MSB)
      cfg.pin_d11 = GPIO_NUM_1;    // Red   bit 3 (LSB)
      cfg.pin_d12 = GPIO_NUM_2;    // Red   bit 4
      cfg.pin_d13 = GPIO_NUM_42;   // Red   bit 5
      cfg.pin_d14 = GPIO_NUM_41;   // Red   bit 6
      cfg.pin_d15 = GPIO_NUM_40;   // Red   bit 7 (MSB)

      cfg.pin_henable = GPIO_NUM_5;   // DE
      cfg.pin_vsync   = GPIO_NUM_3;   // VSYNC (strapping pin — fine after boot)
      cfg.pin_hsync   = GPIO_NUM_46;  // HSYNC (strapping pin — fine after boot)
      cfg.pin_pclk    = GPIO_NUM_7;   // PCLK

      cfg.freq_write = 12000000;  // 12 MHz — lower pclk = less PSRAM scan-out bandwidth
                                  // demand, steadier image on the single framebuffer.
                                  // (Was 16 MHz; raise again only if the image is rock-stable.)

      // Clock polarity:
      //   Bus_RGB default: pclk_active_neg=1 (latch on falling edge), pclk_idle_high=0 (idle LOW)
      //   ESPHome pclk_inverted:true → active on falling edge → matches default pclk_active_neg=1
      //   Leave pclk_idle_high at default (0) for first test.
      //   If display shows nothing: try cfg.pclk_idle_high = 1;
      cfg.pclk_idle_high = 0;  // idle LOW — matches ESPHome pclk_inverted:true
                               // (= pclk_active_neg=1, Bus_RGB default)

      // Sync timings — IMPORTANT: Bus_RGB stores these as int8_t (max 127).
      // The ESPHome YAML values (hpw=162, hbp=152) overflow int8_t and corrupt
      // the timing. Use the known-good values from the EK9716B community config.
      cfg.hsync_polarity    = 0;
      cfg.hsync_front_porch = 40;   // ESPHome: 48  (but int8_t overflow risk)
      cfg.hsync_pulse_width = 48;   // ESPHome: 162 → OVERFLOWED int8_t!
      cfg.hsync_back_porch  = 88;   // ESPHome: 152 → OVERFLOWED int8_t!
      cfg.vsync_polarity    = 0;
      cfg.vsync_front_porch = 3;
      cfg.vsync_pulse_width = 10;   // ESPHome: 45
      cfg.vsync_back_porch  = 18;   // ESPHome: 13

      _bus_instance.config(cfg);
    }
    _panel_instance.setBus(&_bus_instance);
    setPanel(&_panel_instance);
  }
};

static LGFX lcd;

// ─────────────────────────────────────────────────────────────
//  ESP-NOW link + LVGL dashboard state
//
//  Data flow (mirrors projects/client_simple_hud):
//    ESP-NOW recv (WiFi task) → on_payload() buffers under a critical section
//    → loop() drains it and calls app_ui::update() (LVGL — loop task only).
//  ServerConnectionMonitor flips an ONLINE/OFFLINE indicator when packets stop.
// ─────────────────────────────────────────────────────────────
static ESPNowReceiver          g_receiver;
static ServerConnectionMonitor g_conn_monitor;

static portMUX_TYPE  g_payload_mux         = portMUX_INITIALIZER_UNLOCKED;
static Payload       g_pending_payload     = {};
static volatile bool g_has_pending_payload = false;

static volatile bool g_status_dirty  = false;
static volatile bool g_status_online = false;

static inline uint32_t now_ms() { return (uint32_t)(esp_timer_get_time() / 1000); }

// LVGL 9 input — poll the GT911 and hand LVGL the current finger position.
// Runs on the LVGL thread, so it shares Wire with the sensor reads in loop()
// without contention (there is no second task touching I2C).
static void lvgl_touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
  LV_UNUSED(indev);
  uint16_t x = 0, y = 0;
  if (gt911::read(&x, &y)) {
    data->point.x = x;
    data->point.y = y;
    data->state   = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// LVGL 9 flush — push the rendered strip into the LovyanGFX RGB framebuffer.
static void lvgl_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  const int32_t w = area->x2 - area->x1 + 1;
  const int32_t h = area->y2 - area->y1 + 1;
  lcd.startWrite();
  lcd.pushImage(area->x1, area->y1, w, h, reinterpret_cast<uint16_t*>(px_map));
  lcd.endWrite();
  lv_display_flush_ready(disp);
}

// ESP-NOW receive callback — runs in the WiFi task. Buffer only; never touch LVGL.
static void on_payload(const Payload& p) {
  portENTER_CRITICAL(&g_payload_mux);
  g_pending_payload     = p;
  g_has_pending_payload = true;
  portEXIT_CRITICAL(&g_payload_mux);
  g_conn_monitor.onPayloadReceived(now_ms());
}

// Connection-status change — may fire from either task; only sets flags.
static void on_status_change(bool online) {
  DBG("[link] server %s", online ? "ONLINE" : "OFFLINE");
  g_status_online = online;
  g_status_dirty  = true;
}

// Bring up LVGL, build the dashboard, and start the ESP-NOW receiver. Call this
// AFTER lcd.init() so the panel/framebuffer is live.
static void app_init() {
  lv_init();

  // Two 40-line RGB565 partial buffers in INTERNAL SRAM (80 KB each).
  //
  // These MUST NOT live in PSRAM. The RGB peripheral continuously DMA-streams the
  // framebuffer out of PSRAM to the panel; if the flush also reads its source from
  // PSRAM, the two compete for PSRAM bandwidth, the scan-out FIFO underflows and
  // the whole image shakes/jitters. Internal SRAM source removes that contention:
  // the flush reads fast on-chip RAM and only the framebuffer write touches PSRAM.
  // 40 lines keeps each buffer small enough to fit internal RAM with room for WiFi.
  constexpr uint32_t kBufLines = 40;
  constexpr size_t   kBufBytes = 1024u * kBufLines * 2u;  // 2 bytes/px (RGB565)
  void* buf1 = heap_caps_malloc(kBufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  void* buf2 = heap_caps_malloc(kBufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!buf1 || !buf2) {
    DBG("[lvgl] internal draw buffer alloc failed (%u bytes x2) — halting", (unsigned)kBufBytes);
    while (true) delay(100);
  }
  DBG("[lvgl] draw buffers in internal SRAM: 2 x %u bytes, free internal=%u",
      (unsigned)kBufBytes, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

  lv_display_t* disp = lv_display_create(1024, 600);
  // This RGB panel latches the two RGB565 bytes in the opposite order to LVGL's
  // native little-endian layout. Without SWAPPED, neutral greys come out green
  // (e.g. card border 0x2A2A2A → a green-dominant value). SWAPPED makes LVGL
  // emit the byte order the panel framebuffer expects.
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
  lv_display_set_flush_cb(disp, lvgl_flush_cb);
  lv_display_set_buffers(disp, buf1, buf2, kBufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

  // ── Touch input ──
  // Registered even if the GT911 was not found: the read callback then simply
  // never reports a press, and the dashboard renders exactly as before.
  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, lvgl_touch_read_cb);

  app_ui::create();
  app_ui::set_server_status(false);   // start OFFLINE until the first payload
  DBG("[lvgl] SquareLine UI created (1024x600)");

  // ── ESP-NOW link to the server ──
  g_conn_monitor.setStatusChangeCallback(on_status_change);
  g_receiver.setCallback(on_payload);
  bool ok = g_receiver.begin(PMK_KEY);
  DBG("[link] ESPNowReceiver.begin() = %s, channel=%d", ok ? "OK" : "FAIL", WiFi.channel());
}

// ─────────────────────────────────────────────────────────────
//  Arduino entry points
// ─────────────────────────────────────────────────────────────
void setup() {
  // UART0 Serial — same channel as the ROM and [I] core messages
  // (without ARDUINO_USB_CDC_ON_BOOT, Serial maps to UART0, which
  //  the USB-JTAG bridge forwards to USB automatically)
  Serial.begin(115200);
  delay(500);
  DBG("\n\n=== Waveshare ESP32-S3-Touch-LCD-7B ===");

  // ── 1. I2C ────────────────────────────────────────────────
  Wire.begin(DISP_SDA, DISP_SCL);
  Wire.setClock(400000);
  DBG("[I2C] init  SDA=8  SCL=9  400kHz");
  i2c_scan();

  // ── 2. PCF8574 — bit-scan diagnóstico ────────────────────
  // 0x07 (bits 0-2) não acendeu o backlight → varredura de todos
  // os 8 pinos para identificar qual bit realmente controla o BL.
  // Observe a placa: o bit que acender o backlight durante os 2 s
  // é o BL_BIT correto. Anote o número e atualize BL_BIT acima.
  pcf8574_init();
  pcf8574_write(0x00);
  delay(100);

  // DBG("[DIAG] === PCF8574 bit scan (8 janelas de 2 s) ===");
  // DBG("[DIAG] Observe a tela: qual bit acende o backlight?");
  for (uint8_t b = 0; b < 8; b++) {
    uint8_t val = (uint8_t)(1u << b);
    // DBG("[DIAG] bit %d → 0x%02X  HIGH agora...", b, val);
    pcf8574_write(val);
    pcf8574_write(0x00);
    // delay(300);
  }
  // Tenta tudo HIGH como fallback antes do lcd.init()
  // DBG("[DIAG] Scan completo. Escrevendo 0xFF (todos HIGH) antes do lcd.init().");
  pcf8574_write(0xFF);
  // delay(200);

  // ── 2b. Touch ─────────────────────────────────────────────
  // Only now is TP_RST high, so only now can the GT911 answer — the i2c_scan()
  // above runs while it is still held in reset, which is why it never appears
  // there. The chip needs a moment to boot after reset release.
  //
  // BOARD_SPEC §5.2 maps P2 to TP_RST, but only P1 (backlight) was ever actually
  // verified by bit-scan; P0/P2 are unconfirmed guesses. So re-scan here: if the
  // GT911 shows up now, the mapping holds and any failure is in our driver; if it
  // does not, the chip is still in reset and the reset line is not where we think.
  delay(200);
  gt911_bringup();

  // ── 3. Display init ───────────────────────────────────────
  // DBG("[LGFX] calling lcd.init() ...");
  bool ok = lcd.init();
  // DBG("[LGFX] lcd.init() returned %s", ok ? "TRUE  <-- OK" : "FALSE <-- FAILED (check GPIO/PSRAM)");
  if (!ok) {
    // DBG("[LGFX] init failed. Halting.");
    while (true) { delay(100); }
  }
  // DBG("[LGFX] display size: %d x %d", lcd.width(), lcd.height());

  // ── 4. LVGL dashboard + ESP-NOW link ──────────────────────
  app_init();

  // ── 5. GPS (u-blox NEO-6M on UART2) ───────────────────────
  gps::begin();

  // ── 6. MPU6050 IMU (shared I2C bus, 0x68) ─────────────────
  // Uses the Wire bus already brought up in step 1 — non-fatal if absent.
  mpu6050::begin();

  // ── 7. AHT20 + BMP280 env sensor (shared I2C bus, 0x38 / 0x76) ──
  // Uses the Wire bus already brought up in step 1 — non-fatal per chip.
  aht20_bmp280::begin();

  backlight_button_init();

  DBG("[setup] complete");
}

void loop() {
  // ── Drain a payload buffered by the WiFi task → dashboard ──
  if (g_has_pending_payload) {
    Payload local;
    portENTER_CRITICAL(&g_payload_mux);
    local                 = g_pending_payload;
    g_has_pending_payload = false;
    portEXIT_CRITICAL(&g_payload_mux);
    app_ui::update(local);
  }

  // ── Apply a buffered connection-status change ──
  if (g_status_dirty) {
    g_status_dirty = false;
    app_ui::set_server_status(g_status_online);
  }

  // ── Drive LVGL ──
  static uint32_t last_tick = 0;
  uint32_t t = now_ms();
  if (last_tick == 0) last_tick = t;       // avoid a huge first delta
  lv_tick_inc(t - last_tick);
  last_tick = t;
  lv_timer_handler();

  // ── Physical backlight button (GPIO 6) ──
  // Polled on the LVGL thread so cycle_backlight() touches widgets safely.
  backlight_button_tick(t);

  // ── Connection timeout → fires OFFLINE if packets stop arriving ──
  g_conn_monitor.tick(t);

  // ── GPS: drain UART2, parse NMEA, refresh status ~1 Hz (non-blocking) ──
  gps::update(t);
  if (gps::takeDirty()) {                        // LVGL calls: loop task only
    app_ui::set_gps_datetime(gps::dateTimeText());
    app_ui::set_gps_altitude(gps::altitudeText());
    app_ui::set_gps_compass(gps::compassText());
  }

  // ── MPU6050: read accel/gyro/temp, print every 500 ms (non-blocking) ──
  mpu6050::update(t);

  // ── AHT20 + BMP280: read temp/humidity/pressure, print every 500 ms (non-blocking) ──
  aht20_bmp280::update(t);

  // ── MPU6050 IMU → dashboard labels, refresh ~2 Hz (matches sensor cadence) ──
  static uint32_t last_imu_ms = 0;
  if (mpu6050::isReady() && t - last_imu_ms >= 500) {
    last_imu_ms = t;
    app_ui::set_imu(mpu6050::accelX(), mpu6050::accelY(), mpu6050::accelZ(),
                    mpu6050::gyroX(),  mpu6050::gyroY(),  mpu6050::gyroZ());
  }

  // ── Ambient temp + humidity (AHT20) → dashboard labels, refresh ~1 Hz ──
  static uint32_t last_amb_ms = 0;
  if (t - last_amb_ms >= 1000) {
    last_amb_ms = t;
    float tc = aht20_bmp280::ambientTemperatureC();
    char buf[12];
    if (isnan(tc)) snprintf(buf, sizeof(buf), "--");
    else           snprintf(buf, sizeof(buf), "%.1f C", tc);  // "XX.X C" — ° glyph
                                                              // not in ui_font_robotoregular28
    app_ui::set_ambient_temperature(buf);

    float rh = aht20_bmp280::ambientHumidity();
    if (isnan(rh)) snprintf(buf, sizeof(buf), "--");
    else           snprintf(buf, sizeof(buf), "%.0f %%", rh);  // "XX %"
    app_ui::set_humidity(buf);
  }

  // ── Trip time: HH:MM:SS since boot (resets on restart, like TRIP km) ──
  static uint32_t last_trip_ms = 0;
  if (t - last_trip_ms >= 1000) {
    last_trip_ms = t;
    uint32_t s = t / 1000;
    char buf[12];
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
             (unsigned)(s / 3600), (unsigned)((s % 3600) / 60), (unsigned)(s % 60));
    app_ui::set_trip_time(buf);
  }

  // ── Hold the backlight ON ──────────────────────────────────
  // The backlight is armed by the 0xFF PCF8574 write in setup(); re-assert it
  // once a second (never 0x00) so the panel stays lit even if a later I2C access
  // disturbs the latch. This keeps the working part of the original backlight
  // test while replacing its 3 s on/off blink, which would flash a live display.
  static uint32_t last_bl = 0;
  if (t - last_bl >= 1000) {
    last_bl = t;
    pcf8574_write(0xFF);
  }

  delay(5);
}
