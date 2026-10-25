#include "sd_logger.h"

#include <Arduino.h>
#include <SD_MMC.h>

#include "pin_config.h"

namespace sd_logger {
namespace {

constexpr const char* kDir          = "/log";
constexpr uint32_t    kFlushMs      = 10000;           // most data a power cut can lose
constexpr uint32_t    kRotateMs     = 60u * 60u * 1000u;
constexpr uint64_t    kMinFreeBytes = 32ull << 20;     // prune oldest files below this
constexpr size_t      kRowMax       = 128;             // longest row is ~65 B

QueueHandle_t g_queue = nullptr;
File          g_file;
char          g_buf[4096];  // > 10 s of rows
size_t        g_len = 0;

/** Deletes the alphabetically (= chronologically) first log file; false if none or the delete failed. */
bool delete_oldest() {
  File dir = SD_MMC.open(kDir);
  String oldest;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    const char* name = f.name();
    if (!f.isDirectory() && log_is_name(name) &&
        (oldest.isEmpty() || strcmp(name, oldest.c_str()) < 0))
      oldest = name;
  }
  if (oldest.isEmpty()) return false;
  Serial.printf("[sdlog] card low on space, deleting %s\n", oldest.c_str());
  return SD_MMC.remove(String(kDir) + "/" + oldest);
}

/** Closes the current file, frees space, and opens a file named after f; false if no space can be freed. */
bool open_next(const gps::Fix& f) {
  if (g_file) g_file.close();
  while (SD_MMC.totalBytes() - SD_MMC.usedBytes() < kMinFreeBytes)
    if (!delete_oldest()) return false;

  char name[32];
  log_file_name(f, name, sizeof(name));
  g_file = SD_MMC.open(String(kDir) + "/" + name, FILE_APPEND);  // same-second reboot appends
  if (!g_file) return false;
  if (g_file.size() == 0) g_file.print(LOG_CSV_HEADER);
  Serial.printf("[sdlog] logging to %s/%s\n", kDir, name);
  return true;
}

/** Writes the buffered rows and fsyncs, so they survive a power cut. */
void flush() {
  if (g_len == 0) return;
  g_file.write(reinterpret_cast<const uint8_t*>(g_buf), g_len);
  g_file.flush();
  g_len = 0;
}

/** Writer task: batches queued samples into the current file. */
void writer_task(void*) {
  uint32_t last_flush = millis();
  uint32_t opened_at  = 0;
  bool     open       = false;
  LogSample s;

  for (;;) {
    const bool got = xQueueReceive(g_queue, &s, pdMS_TO_TICKS(1000)) == pdTRUE;
    const uint32_t now = millis();

    if (got) {
      if (!open || now - opened_at >= kRotateMs) {
        flush();
        open      = open_next(s.gps);
        opened_at = now;
        if (!open) {
          Serial.printf("[sdlog] cannot open a log file (card full?) — logging stopped\n");
          vTaskDelete(nullptr);
        }
      }
      if (sizeof(g_buf) - g_len < kRowMax) flush();
      g_len += log_format_row(s, g_buf + g_len, sizeof(g_buf) - g_len);
    }

    if (now - last_flush >= kFlushMs) {
      flush();
      last_flush = now;
    }
  }
}

}  // namespace

bool begin() {
  // 20 MHz: the slot is laid out for SPI, and a log needs a few hundred B/s.
  if (!SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN) ||
      !SD_MMC.begin("/sdcard", /*mode1bit=*/true, /*format_if_mount_failed=*/false,
                    SDMMC_FREQ_DEFAULT)) {
    Serial.printf("[sdlog] no SD card mounted — logging off\n");
    return false;
  }
  SD_MMC.mkdir(kDir);
  Serial.printf("[sdlog] card %llu MB\n", SD_MMC.cardSize() >> 20);

  g_queue = xQueueCreate(16, sizeof(LogSample));
  xTaskCreatePinnedToCore(writer_task, "sdlog", 6144, nullptr, 1, nullptr, 0);
  return true;
}

void push(const LogSample& s) {
  if (g_queue) xQueueSend(g_queue, &s, 0);
}

}  // namespace sd_logger
