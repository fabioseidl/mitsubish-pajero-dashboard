#pragma once

#include <stdint.h>

// ─────────────────────────────────────────────────────────────
//  GPS module — ATGM336H / NEO-6M (NMEA 0183 @ 9600 bps)
//
//  Receives, decodes and validates NMEA sentences with TinyGPSPlus,
//  formats the parsed fix, and exposes it as text for the LVGL UI
//  (also echoed to Serial when a console is attached).
//
//  Pure consumer: begin() wires up the UART + parser; update() must be
//  pumped from the main loop. No blocking delays, no per-loop heap.
//
//  Pin assignment (Option B — see BOARD_SPEC.md §3.5):
//    The board's "UART2" connector is wired to ESP32 UART0 (GPIO43/44),
//    selected via the onboard UART-selection DIP switch. With the switch
//    set to UART2, the GPS TX lands on GPIO44 and the USB-C console is
//    disconnected (they share UART0) — so the fix is shown on the LCD.
//    We read GPIO44 with the UART2 peripheral (input-only).
// ─────────────────────────────────────────────────────────────

namespace gps {

// UART wiring. Single source of truth for the pins; override at build time.
#ifndef GPS_UART_NUM
#define GPS_UART_NUM 2          // ESP32 UART2 peripheral, routed to GPIO44
#endif
#ifndef GPS_RX_PIN
#define GPS_RX_PIN 44           // ESP32-S3 RX ← GPS TX (UART2 connector = UART0 RXD)
#endif
#ifndef GPS_TX_PIN
#define GPS_TX_PIN -1           // read-only; never drive the shared UART0 TX
#endif
#ifndef GPS_BAUD
#define GPS_BAUD 9600
#endif

// Local time offset applied to the UTC timestamp. Fixed UTC-3 (no DST).
#ifndef GPS_UTC_OFFSET_HOURS
#define GPS_UTC_OFFSET_HOURS (-3)
#endif

// Optional one-time RX-pin auto-scan at startup (debug aid). Off for Option B:
// the connector is known to be GPIO44. Set to 1 to probe the free pins instead.
#ifndef GPS_AUTODETECT_RX
#define GPS_AUTODETECT_RX 0
#endif

// Initialise the UART and the NMEA parser. Call once from setup().
void begin();

// Feed any waiting UART bytes into the parser and, ~once per second, refresh the
// formatted status text. Call every loop iteration; it never blocks.
void update(uint32_t now_ms);

// Latest compact status line (link health + fix + sats) for a debug label.
const char* statusText();

// Per-field text for dedicated dashboard labels (stable buffers; "--" if invalid).
const char* dateTimeText();  // "YYYY-MM-DD HH:MM:SS" in UTC-3
const char* altitudeText();  // altitude MSL in metres
const char* compassText();   // course over ground in degrees

// True exactly once after each ~1 Hz status refresh — poll it to know when to
// push statusText() to the display.
bool takeDirty();

}  // namespace gps
