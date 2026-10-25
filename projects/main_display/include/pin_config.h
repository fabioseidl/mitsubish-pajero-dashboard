#pragma once

// ── microSD slot (SDMMC 1-bit) ───────────────────────────────
// Waveshare wires it for SPI (MOSI=11, SCK=12, MISO=13, CS=expander EXIO4).
// SDMMC 1-bit uses the same lines as CMD/CLK/D0 and needs no CS: D3/CS only has
// to stay high, which the expander's 0xFF hold in main.cpp already does.
#define SD_CMD_PIN 11
#define SD_CLK_PIN 12
#define SD_D0_PIN  13
