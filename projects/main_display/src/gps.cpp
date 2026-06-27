// ============================================================
//  GPS module — ATGM336H / NEO-6M (see gps.h)
//
//  Receive → decode (TinyGPSPlus) → validate → format.
//  Builds a multi-line status string each ~1 s for the LVGL UI
//  (Option B: the GPS shares UART0 with the USB console via the
//  board's UART-selection switch, so output goes to the LCD).
// ============================================================

#include "gps.h"

#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <stdarg.h>
#include <string.h>

namespace gps {
namespace {

// ── Static state (no per-loop allocation) ────────────────────
HardwareSerial gpsSerial(GPS_UART_NUM);
TinyGPSPlus    parser;

// "Satellites in view" is not exposed by TinyGPSPlus directly. It lives in the
// GSV sentences (field 3 = satellites in view). We pull the count from $GPGSV.
// (The ATGM336H also emits $BDGSV for BeiDou, which this does not sum — so the
// visible count is GPS-only.)
TinyGPSCustom satsInView(parser, "GPGSV", 3);

uint32_t lastRefreshMs   = 0;
uint32_t lastRxMs        = 0;
int8_t   activeRxPin     = GPS_RX_PIN;

char     statusBuf[160]  = "GPS: starting...";
bool     statusDirty     = true;
size_t   appendPos       = 0;

// Per-field text for the dedicated dashboard labels (stable buffers for LVGL).
char     dateTimeBuf[24] = "--";   // "YYYY-MM-DD HH:MM:SS" (UTC-3)
char     altitudeBuf[12] = "--";   // metres MSL
char     compassBuf[12]  = "--";   // course over ground, degrees

constexpr uint32_t kRefreshIntervalMs = 1000;  // ~1 Hz, matches GPS update rate
constexpr uint32_t kNoDataTimeoutMs   = 5000;  // note in the status if UART silent

void appendStatus(const char* fmt, ...) {
  if (appendPos >= sizeof(statusBuf)) return;
  va_list ap;
  va_start(ap, fmt);
  int w = vsnprintf(statusBuf + appendPos, sizeof(statusBuf) - appendPos, fmt, ap);
  va_end(ap);
  if (w > 0) appendPos += static_cast<size_t>(w);
}

#if GPS_AUTODETECT_RX
// ── Optional RX-pin auto-scan (debug aid; disabled for Option B) ──
constexpr int8_t kCandidateRxPins[] = {6, 11, 12, 13};
struct ProbeResult { uint32_t bytes; uint16_t dollars; };

ProbeResult probeRx(int8_t rxPin, uint32_t baud, bool invert, uint32_t durationMs) {
  gpsSerial.end();
  gpsSerial.begin(baud, SERIAL_8N1, rxPin, /*tx=*/-1, invert);
  ProbeResult r{0, 0};
  const uint32_t deadline = millis() + durationMs;
  while ((int32_t)(millis() - deadline) < 0) {
    while (gpsSerial.available() > 0) {
      char c = (char)gpsSerial.read();
      ++r.bytes;
      if (c == '$') ++r.dollars;
    }
  }
  return r;
}

int8_t autodetectRxPin() {
  Serial.println("[gps] auto-detecting RX pin...");
  int8_t best = -1; uint16_t bestSeen = 0;
  for (int8_t pin : kCandidateRxPins) {
    ProbeResult r = probeRx(pin, GPS_BAUD, false, 1200);
    Serial.printf("[gps] probe GPIO%-2d : %lu bytes, %u NMEA starts\n",
                  pin, (unsigned long)r.bytes, r.dollars);
    if (r.dollars > bestSeen) { bestSeen = r.dollars; best = pin; }
  }
  return (best >= 0 && bestSeen >= 2) ? best : -1;
}
#endif  // GPS_AUTODETECT_RX

// ── UTC → local civil time (fixed offset, no DST) ────────────
// Howard Hinnant's days-from-civil algorithm: correct day/month/year rollover
// (and underflow for a negative offset) without mktime/timegm.
struct CivilTime {
  int32_t year; uint8_t month, day, hour, minute, second;
};

int64_t daysFromCivil(int32_t y, uint32_t m, uint32_t d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = static_cast<uint32_t>(y - era * 400);
  const uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

CivilTime civilFromDays(int64_t z) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const uint32_t doe = static_cast<uint32_t>(z - era * 146097);
  const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int32_t y = static_cast<int32_t>(yoe) + static_cast<int32_t>(era) * 400;
  const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const uint32_t mp = (5 * doy + 2) / 153;
  const uint32_t d = doy - (153 * mp + 2) / 5 + 1;
  const uint32_t m = mp + (mp < 10 ? 3 : -9);
  CivilTime ct;
  ct.year = y + (m <= 2);
  ct.month = static_cast<uint8_t>(m);
  ct.day = static_cast<uint8_t>(d);
  return ct;
}

CivilTime toLocalTime(TinyGPSDate& date, TinyGPSTime& time) {
  int64_t secs = daysFromCivil(date.year(), date.month(), date.day()) * 86400LL;
  secs += static_cast<int64_t>(time.hour()) * 3600
        + static_cast<int64_t>(time.minute()) * 60 + time.second();
  secs += static_cast<int64_t>(GPS_UTC_OFFSET_HOURS) * 3600;
  int64_t days = secs / 86400;
  int32_t rem  = static_cast<int32_t>(secs - days * 86400);
  if (rem < 0) { rem += 86400; --days; }
  CivilTime ct = civilFromDays(days);
  ct.hour   = static_cast<uint8_t>(rem / 3600);
  ct.minute = static_cast<uint8_t>((rem % 3600) / 60);
  ct.second = static_cast<uint8_t>(rem % 60);
  return ct;
}

// Refresh the compact status line (for the debug label) and the per-field text
// buffers (for the dedicated dashboard labels). ASCII only — "--" when invalid.
void refreshStatus(uint32_t now_ms) {
  const bool hasFix = parser.location.isValid() && parser.satellites.value() > 0;
  const bool everRx = lastRxMs != 0;
  const bool stale  = everRx && (now_ms - lastRxMs) > kNoDataTimeoutMs;
  const uint32_t usedSats = parser.satellites.isValid() ? parser.satellites.value() : 0;

  // Date/Time, converted to UTC-3.
  if (parser.date.isValid() && parser.time.isValid()) {
    CivilTime t = toLocalTime(parser.date, parser.time);
    snprintf(dateTimeBuf, sizeof(dateTimeBuf), "%04ld-%02u-%02u %02u:%02u:%02u",
             (long)t.year, t.month, t.day, t.hour, t.minute, t.second);
  } else {
    strcpy(dateTimeBuf, "--");
  }

  // Altitude (MSL) and course need a fix to be meaningful.
  if (hasFix && parser.altitude.isValid())
    snprintf(altitudeBuf, sizeof(altitudeBuf), "%.0f", parser.altitude.meters());
  else
    strcpy(altitudeBuf, "--");

  if (hasFix && parser.course.isValid())
    snprintf(compassBuf, sizeof(compassBuf), "%.0f", parser.course.deg());
  else
    strcpy(compassBuf, "--");

  // Compact one-line status for the debug label (link health + fix + sats).
  appendPos = 0;
  statusBuf[0] = '\0';
  appendStatus("GPS %s  sats:%lu  chars:%lu ok:%lu fail:%lu%s",
               hasFix ? "FIX" : "no fix",
               (unsigned long)usedSats,
               (unsigned long)parser.charsProcessed(),
               (unsigned long)parser.passedChecksum(),
               (unsigned long)parser.failedChecksum(),
               (!everRx ? "  [NO DATA]" : (stale ? "  [STALLED]" : "")));

  statusDirty = true;
  Serial.println(statusBuf);   // visible if a console is attached (switch on USB)
}

}  // namespace

void begin() {
#if GPS_AUTODETECT_RX
  int8_t detected = autodetectRxPin();
  if (detected >= 0) activeRxPin = detected;
#endif
  // Receive-only (TX unmapped): never drive the shared UART0 TX (GPIO43).
  gpsSerial.end();
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, activeRxPin, /*tx=*/-1);
  Serial.printf("[gps] UART%d init  RX=GPIO%d  %d 8N1 (rx-only)\n",
                GPS_UART_NUM, activeRxPin, GPS_BAUD);
}

void update(uint32_t now_ms) {
  // Drain the UART RX FIFO into the parser. encode() tolerates partial/invalid
  // sentences and checksum errors — it simply ignores them.
  while (gpsSerial.available() > 0) {
    parser.encode(static_cast<char>(gpsSerial.read()));
    lastRxMs = now_ms;
  }

  if (now_ms - lastRefreshMs >= kRefreshIntervalMs) {
    lastRefreshMs = now_ms;
    refreshStatus(now_ms);
  }
}

const char* statusText()   { return statusBuf; }
const char* dateTimeText() { return dateTimeBuf; }   // "YYYY-MM-DD HH:MM:SS" or "--"
const char* altitudeText() { return altitudeBuf; }   // metres MSL or "--"
const char* compassText()  { return compassBuf; }    // course degrees or "--"

bool takeDirty() {
  if (!statusDirty) return false;
  statusDirty = false;
  return true;
}

}  // namespace gps
