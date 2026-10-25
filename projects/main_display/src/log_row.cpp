#include "log_row.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

size_t log_format_row(const LogSample& s, char* buf, size_t cap) {
  const gps::Fix& g = s.gps;
  char lat[16] = "", lon[16] = "", alt[12] = "", spd[12] = "", rpm[8] = "";
  if (g.loc_valid) {
    snprintf(lat, sizeof(lat), "%.6f", g.lat);  // 6 decimals = 0.1 m
    snprintf(lon, sizeof(lon), "%.6f", g.lon);
    if (!isnan(g.alt_m)) snprintf(alt, sizeof(alt), "%.0f", g.alt_m);
  }
  if (s.link_valid) {
    snprintf(spd, sizeof(spd), "%.1f", s.speed_kmh);
    snprintf(rpm, sizeof(rpm), "%u", (unsigned)s.rpm);
  }
  const int n = snprintf(buf, cap, "%04ld-%02u-%02u %02u:%02u:%02u,%s,%s,%s,%s,%s\n",
                         (long)g.year, g.mon, g.day, g.hh, g.mm, g.ss, lat, lon, alt, spd, rpm);
  return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

void log_file_name(const gps::Fix& f, char* buf, size_t cap) {
  snprintf(buf, cap, "%04ld-%02u-%02u_%02u-%02u-%02u.csv",
           (long)f.year, f.mon, f.day, f.hh, f.mm, f.ss);
}

bool log_is_name(const char* name) {
  static const char kPattern[] = "0000-00-00_00-00-00.csv";  // '0' = any digit
  if (strlen(name) != sizeof(kPattern) - 1) return false;
  for (size_t i = 0; kPattern[i]; i++) {
    const bool ok = kPattern[i] == '0' ? isdigit((unsigned char)name[i]) != 0
                                       : name[i] == kPattern[i];
    if (!ok) return false;
  }
  return true;
}
