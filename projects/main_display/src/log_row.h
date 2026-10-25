#pragma once

// Trip-log CSV formatting — pure, no Arduino, so test/host builds it.

#include <stddef.h>
#include <stdint.h>

#include "gps.h"

/** One trip-log sample: the GPS fix plus the server's speed and rpm. */
struct LogSample {
  gps::Fix gps;
  bool     link_valid;  // server online with CAN data; speed/rpm usable
  float    speed_kmh;
  uint16_t rpm;
};

#define LOG_CSV_HEADER "datetime,lat,lon,alt_m,speed_kmh,rpm\n"

/** Formats s as one CSV line (empty cells for unknown values); returns its length, 0 if cap is too small. */
size_t log_format_row(const LogSample& s, char* buf, size_t cap);

/** Writes the name of a log started at f's time: "YYYY-MM-DD_HH-MM-SS.csv". */
void log_file_name(const gps::Fix& f, char* buf, size_t cap);

/** True only for names log_file_name() produces, so pruning never deletes foreign files. */
bool log_is_name(const char* name);
