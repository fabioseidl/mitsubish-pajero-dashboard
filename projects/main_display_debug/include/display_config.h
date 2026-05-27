#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// Physical display dimensions
// ---------------------------------------------------------------------------
static constexpr uint32_t LCD_H_RES = 1024;
static constexpr uint32_t LCD_V_RES = 600;

// ---------------------------------------------------------------------------
// UI layout constants
// ---------------------------------------------------------------------------
static constexpr uint32_t HEADER_H = 40;               // top status bar height (px)
static constexpr uint32_t BODY_H   = LCD_V_RES - HEADER_H;  // scrollable PID area height

// ---------------------------------------------------------------------------
// LVGL draw buffers
// Both buffers are allocated in PSRAM at runtime (see debug_screen.cpp).
// 2 × (1024 × 60 × 2 bytes) = 2 × 122,880 bytes ≈ 240 KB total.
//
// NOTE: The ESP32-S3 RGB LCD peripheral and OPI PSRAM share the external
// memory bus. ESP32_Display_Panel handles the required bounce buffer
// internally; if the display flickers or shows corruption, increase the
// panel's bounce_buffer_size_px in the board custom header.
// ---------------------------------------------------------------------------
static constexpr uint32_t LVGL_BUF_LINES  = 60;
static constexpr uint32_t LVGL_BUF_PIXELS = LCD_H_RES * LVGL_BUF_LINES;
static constexpr uint32_t LVGL_BUF_BYTES  = LVGL_BUF_PIXELS * 2;  // RGB565 = 2 bytes/px

// ---------------------------------------------------------------------------
// LVGL table layout for the PID grid
// ---------------------------------------------------------------------------

// Two columns of PIDs side-by-side; each column has 3 table-columns:
//   col 0: label   (~220 px)
//   col 1: value   (~100 px)
//   col 2: unit    ( ~60 px)
// Repeated for the right half (cols 3, 4, 5).
// Total: 2 × (220+100+60) = 760 px + separator borders ≈ 1024 px wide.
static constexpr int TABLE_COL_COUNT     = 6;
static constexpr int TABLE_COL_W_LABEL   = 220;
static constexpr int TABLE_COL_W_VALUE   = 100;
static constexpr int TABLE_COL_W_UNIT    =  60;
