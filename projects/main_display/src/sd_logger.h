#pragma once

// ─────────────────────────────────────────────────────────────
//  Trip log on the microSD card
//
//  One CSV per trip in /log, named after its first sample
//  ("YYYY-MM-DD_HH-MM-SS.csv") and split hourly. A low-priority task on
//  core 0 does the SD I/O so card latency never stalls LVGL. Rows are
//  synced every 10 s; the oldest files are deleted when free space runs low.
// ─────────────────────────────────────────────────────────────

#include "log_row.h"

namespace sd_logger {

/** Mounts the card and starts the writer task; false (logger stays off) if no card mounts. */
bool begin();

/** Queues one sample for the writer; drops it if the queue is full or the logger is off. */
void push(const LogSample& s);

}  // namespace sd_logger
