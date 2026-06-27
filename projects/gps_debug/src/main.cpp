// ============================================================
//  gps_debug — minimal GPS bring-up sketch
//
//  No display, no LVGL, no ESP-NOW. Just: open UART2 on a pin,
//  echo whatever bytes arrive (so you can SEE the raw NMEA), and
//  parse it with TinyGPSPlus. A boot-time scan reports which of
//  the board's free GPIOs (6, 11, 12, 13) is receiving data.
//
//  Board: Waveshare ESP32-S3-Touch-LCD-7B. Per Waveshare's pinout
//  the only free native GPIOs are 6/11/12/13 (LCD takes the rest,
//  Touch 4/8/9, RS-485 15/16, CAN 19/20, UART0 console 43/44).
//  GPIO6 = the `gp6` pin on the "GPIO" PH2.0 header.
//
//  Wire the GPS: TX -> chosen GPIO, GND -> board GND, VCC -> 3v3.
// ============================================================

#include <Arduino.h>
#include <TinyGPSPlus.h>

// ── Config ───────────────────────────────────────────────────
// Console-debug path: the GPS must be on a free native GPIO, NOT the "UART2"
// connector. That connector is ESP32 UART0 (GPIO43/44) and the DIP switch routes
// UART0 to EITHER the USB-C console OR the connector — never both, so reading
// the GPS there kills the PC console. The free native pins are 6/11/12/13;
// GPIO6 = the `gp6` pin on the "GPIO" PH2.0 header. Keep the switch on USB.
static constexpr uint32_t kBaud   = 9600;            // ATGM336H / NEO-6M default
static constexpr int8_t   kRxPins[] = {6, 11, 12, 13};
static constexpr int8_t   kDefaultRxPin = 6;         // `gp6` on the GPIO header
static constexpr bool     kEchoRaw = true;           // echo received bytes to console

HardwareSerial GpsSerial(2);   // UART2 (receive-only here)
TinyGPSPlus    gps;

static int8_t   g_rxPin    = kDefaultRxPin;
static uint32_t g_lastSummaryMs = 0;
static uint32_t g_rawBytes = 0;

// Listen on `pin` for durationMs and return the byte count, printing a short
// sample of what arrived. Receive-only (TX unmapped) so no pin gets driven.
static uint32_t probePin(int8_t pin, uint32_t durationMs) {
  GpsSerial.end();
  GpsSerial.begin(kBaud, SERIAL_8N1, pin, /*tx=*/-1);
  uint32_t bytes = 0, dollars = 0;
  char sample[33]; uint8_t s = 0;
  uint32_t deadline = millis() + durationMs;
  while ((int32_t)(millis() - deadline) < 0) {
    while (GpsSerial.available() > 0) {
      char c = (char)GpsSerial.read();
      ++bytes;
      if (c == '$') ++dollars;
      if (s < sizeof(sample) - 1 && c >= 32 && c < 127) sample[s++] = c;
    }
  }
  sample[s] = '\0';
  Serial.printf("  GPIO%-2d : %4lu bytes, %lu NMEA starts  sample=\"%s\"\n",
                pin, (unsigned long)bytes, (unsigned long)dollars, sample);
  return bytes;
}

static int8_t scanForGps() {
  Serial.println("[scan] probing free GPIOs (6, 11, 12, 13) @ 9600 8N1, 1.5 s each...");
  int8_t   best = -1;
  uint32_t bestBytes = 0;
  for (int8_t pin : kRxPins) {
    uint32_t bytes = probePin(pin, 1500);
    if (bytes > bestBytes) { bestBytes = bytes; best = pin; }
  }
  if (best >= 0 && bestBytes >= 10) {
    Serial.printf("[scan] >>> GPS appears to be on GPIO%d (%lu bytes)\n",
                  best, (unsigned long)bestBytes);
    return best;
  }
  Serial.printf("[scan] no real data on any free pin — defaulting to GPIO%d.\n",
                kDefaultRxPin);
  Serial.println("[scan] Wire GPS TX -> that pin, GND -> board GND, VCC -> 3v3.");
  return kDefaultRxPin;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n\n=== gps_debug (minimal) ===");
  Serial.printf("baud=%lu  echo_raw=%d\n", (unsigned long)kBaud, kEchoRaw);

  g_rxPin = scanForGps();

  GpsSerial.end();
  GpsSerial.begin(kBaud, SERIAL_8N1, g_rxPin, /*tx=*/-1);
  Serial.printf("[gps] listening on GPIO%d. Raw bytes echoed below; summary every 1 s.\n",
                g_rxPin);
  Serial.println("------------------------------------------------------------");
}

void loop() {
  // Drain UART2 → echo raw + feed the parser.
  while (GpsSerial.available() > 0) {
    char c = (char)GpsSerial.read();
    ++g_rawBytes;
    if (kEchoRaw) Serial.write(c);   // shows the literal NMEA stream
    gps.encode(c);
  }

  // Once per second, print a parsed summary.
  uint32_t now = millis();
  if (now - g_lastSummaryMs >= 1000) {
    g_lastSummaryMs = now;
    Serial.printf(
      "\n[summary] rx_bytes=%lu  chars=%lu  checksum ok=%lu fail=%lu  fix=%s  "
      "sats=%lu  lat=%.6f lon=%.6f alt=%.1f\n",
      (unsigned long)g_rawBytes,
      (unsigned long)gps.charsProcessed(),
      (unsigned long)gps.passedChecksum(),
      (unsigned long)gps.failedChecksum(),
      (gps.location.isValid() ? "YES" : "no"),
      (unsigned long)(gps.satellites.isValid() ? gps.satellites.value() : 0),
      (gps.location.isValid() ? gps.location.lat() : 0.0),
      (gps.location.isValid() ? gps.location.lng() : 0.0),
      (gps.altitude.isValid() ? gps.altitude.meters() : 0.0));
  }
}
