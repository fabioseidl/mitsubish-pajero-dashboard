/**
 * LVGL 9.5.0 configuration for Waveshare ESP32-S3-Touch-LCD-7B (1024x600 RGB565).
 *
 * This is a hand-written dashboard build — there is no SquareLine Studio export.
 * Only the widgets/fonts/layouts the dashboard actually uses are enabled.
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/* RGB565, no byte-swap — matches BOARD_SPEC §15 (LV_COLOR_16_SWAP 0).
 * Pixels are pushed to the LovyanGFX RGB framebuffer in native order. */
#define LV_COLOR_DEPTH 16

/* Memory — delegate to the system malloc/free so LVGL's allocations land in the
 * ESP32 heap (which includes PSRAM on this board) rather than a static array. */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

/* HAL */
#define LV_DEF_REFR_PERIOD  33
#define LV_DPI_DEF          130

/* OS — LVGL runs single-threaded from the Arduino loop */
#define LV_USE_OS   LV_OS_NONE

/* Rendering */
#define LV_DRAW_BUF_STRIDE_ALIGN        1
#define LV_DRAW_BUF_ALIGN               4
#define LV_DRAW_LAYER_SIMPLE_BUF_SIZE   (16 * 1024)

#define LV_USE_DRAW_SW 1
#if LV_USE_DRAW_SW
    #define LV_DRAW_SW_SUPPORT_RGB565           1
    #define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED   1  /* panel needs byte-swapped RGB565 */
    #define LV_DRAW_SW_SUPPORT_RGB565A8         0
    #define LV_DRAW_SW_SUPPORT_RGB888           0
    #define LV_DRAW_SW_SUPPORT_XRGB8888         0
    #define LV_DRAW_SW_SUPPORT_ARGB8888         0
    #define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0
    #define LV_DRAW_SW_SUPPORT_L8               0
    #define LV_DRAW_SW_SUPPORT_AL88             0
    #define LV_DRAW_SW_SUPPORT_A8               1  /* anti-aliased font glyphs */
    #define LV_DRAW_SW_SUPPORT_I1               0
    #define LV_DRAW_SW_I1_LUM_THRESHOLD         127
    #define LV_DRAW_SW_DRAW_UNIT_CNT            1
    #define LV_USE_DRAW_ARM2D_SYNC              0
    #define LV_USE_NATIVE_HELIUM_ASM            0
    #define LV_DRAW_SW_COMPLEX                  1
    #if LV_DRAW_SW_COMPLEX
        #define LV_DRAW_SW_SHADOW_CACHE_SIZE    0
        #define LV_DRAW_SW_CIRCLE_CACHE_SIZE    4
    #endif
    #define LV_USE_DRAW_SW_ASM                  LV_DRAW_SW_ASM_NONE
    #define LV_USE_DRAW_SW_COMPLEX_GRADIENTS    0
#endif

#define LV_USE_NEMA_GFX     0
#define LV_USE_PXP          0
#define LV_USE_G2D          0
#define LV_USE_DRAW_DAVE2D  0
#define LV_USE_DRAW_SDL     0
#define LV_USE_DRAW_VG_LITE 0

/* Logging — off in production; set LV_USE_LOG 1 to debug */
#define LV_USE_LOG      0
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

/* Abort on assertion failure — produces a guru-meditation backtrace in monitor */
#define LV_ASSERT_HANDLER_INCLUDE <stdlib.h>
#define LV_ASSERT_HANDLER         abort();

/* Misc */
#define LV_SPRINTF_CUSTOM   0
#define LV_USE_FLOAT        1
#define LV_USE_MATRIX       0

/* Animation / Timers */
#define LV_USE_ANIM         1
#define LV_USE_TIMER        1
#define LV_USE_SYSMON       0
#define LV_USE_PROFILER     0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR  0

/* Widgets — the dashboard is built from labels only. */
#define LV_USE_ARC       0
#define LV_USE_LABEL     1
#define LV_USE_IMAGE     1  /* required by lv_animimg even when unused */
#define LV_USE_ANIMIMG   0
#define LV_USE_BTN       0
#define LV_USE_BTNMATRIX 0
#define LV_USE_BAR       1
#define LV_USE_SLIDER    0
#define LV_USE_TABLE     0
#define LV_USE_CHECKBOX  0
#define LV_USE_DROPDOWN  0
#define LV_USE_ROLLER    0
#define LV_USE_TEXTAREA  0
#define LV_USE_CHART     0
#define LV_USE_CALENDAR  0
#define LV_USE_IMGBTN    0
#define LV_USE_KEYBOARD  0
#define LV_USE_LED       0
#define LV_USE_LIST      0
#define LV_USE_MENU      0
#define LV_USE_METER     0
#define LV_USE_MSGBOX    0
#define LV_USE_SPAN      0
#define LV_USE_SPINBOX   0
#define LV_USE_SPINNER   0
#define LV_USE_TABVIEW   0
#define LV_USE_TILEVIEW  0
#define LV_USE_WIN       0
#define LV_USE_LINE      1  /* required by the default-enabled lv_scale widget */
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_CANVAS    0
#define LV_USE_OBJ_ID    0

/* Fonts — built-in Montserrat sizes used by the dashboard.
 *   14 — default / fallback
 *   16 — block titles
 *   20 — server status line
 *   28 — info-grid values
 *   48 — large SPEED / RPM values (significantly bigger than the grid) */
#define LV_FONT_MONTSERRAT_8    0
#define LV_FONT_MONTSERRAT_10   0
#define LV_FONT_MONTSERRAT_12   0
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_18   0
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_MONTSERRAT_22   0
#define LV_FONT_MONTSERRAT_24   0
#define LV_FONT_MONTSERRAT_26   0
#define LV_FONT_MONTSERRAT_28   1
#define LV_FONT_MONTSERRAT_30   0
#define LV_FONT_MONTSERRAT_32   0
#define LV_FONT_MONTSERRAT_34   0
#define LV_FONT_MONTSERRAT_36   0
#define LV_FONT_MONTSERRAT_38   0
#define LV_FONT_MONTSERRAT_40   0
#define LV_FONT_MONTSERRAT_42   0
#define LV_FONT_MONTSERRAT_44   0
#define LV_FONT_MONTSERRAT_46   0
#define LV_FONT_MONTSERRAT_48   1
#define LV_FONT_UNSCII_8        0
#define LV_FONT_UNSCII_16       0
#define LV_FONT_DEFAULT         &lv_font_montserrat_14

/* Theme — dark theme matches the black-background automotive style. */
#define LV_USE_THEME_DEFAULT    1
#if LV_USE_THEME_DEFAULT
    #define LV_THEME_DEFAULT_DARK            1
    #define LV_THEME_DEFAULT_GROW            0
    #define LV_THEME_DEFAULT_TRANSITION_TIME 0
#endif
#define LV_USE_THEME_SIMPLE     0
#define LV_USE_THEME_MONO       0

/* Layout — flex is used for the rows; grid disabled (not needed). */
#define LV_USE_FLEX     1
#define LV_USE_GRID     0

/* Drivers — display + touch are wired manually, not via LVGL's driver layer */
#define LV_USE_SDL          0
#define LV_USE_LINUX_FBDEV  0
#define LV_USE_LINUX_DRM    0
#define LV_USE_OPENGLES     0
#define LV_USE_WINDOWS      0
#define LV_USE_EVDEV        0
#define LV_USE_LIBINPUT     0
#define LV_USE_X11          0
#define LV_USE_WAYLAND      0

/* LovyanGFX 1.x bundles font headers that reference glyph-format values which
 * may be absent from this LVGL version. The #ifndef guards are safe: if LVGL
 * already defines these they are untouched; otherwise they become no-op macros. */
#ifndef LV_FONT_GLYPH_FORMAT_A1_ALIGNED
#define LV_FONT_GLYPH_FORMAT_A1_ALIGNED 0x011
#endif
#ifndef LV_FONT_GLYPH_FORMAT_A2_ALIGNED
#define LV_FONT_GLYPH_FORMAT_A2_ALIGNED 0x012
#endif
#ifndef LV_FONT_GLYPH_FORMAT_A4_ALIGNED
#define LV_FONT_GLYPH_FORMAT_A4_ALIGNED 0x014
#endif

#endif /* LV_CONF_H */
