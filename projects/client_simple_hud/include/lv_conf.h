/**
 * @file lv_conf.h
 * Minimal LVGL 9.5.0 configuration for CYD ILI9341 320x240
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/* RGB565 — ILI9341 native format */
#define LV_COLOR_DEPTH 16

/* Memory */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_BUILTIN
#define LV_MEM_SIZE             (64 * 1024U)
#define LV_MEM_POOL_EXPAND_SIZE 0
#define LV_MEM_ADR              0

/* HAL */
#define LV_DEF_REFR_PERIOD  33
#define LV_DPI_DEF          130

/* OS — single-task, no RTOS integration needed in LVGL */
#define LV_USE_OS   LV_OS_NONE

/* Rendering */
#define LV_DRAW_BUF_STRIDE_ALIGN        1
#define LV_DRAW_BUF_ALIGN               4
#define LV_DRAW_TRANSFORM_USE_MATRIX    0
#define LV_DRAW_LAYER_SIMPLE_BUF_SIZE   (8 * 1024)
#define LV_DRAW_LAYER_MAX_MEMORY        0
#define LV_DRAW_THREAD_STACK_SIZE       (8 * 1024)
#define LV_DRAW_THREAD_PRIO             LV_THREAD_PRIO_HIGH

#define LV_USE_DRAW_SW 1
#if LV_USE_DRAW_SW
    #define LV_DRAW_SW_SUPPORT_RGB565           1
    #define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED   1  /* ILI9341 native */
    #define LV_DRAW_SW_SUPPORT_RGB565A8         0
    #define LV_DRAW_SW_SUPPORT_RGB888           0
    #define LV_DRAW_SW_SUPPORT_XRGB8888         0
    #define LV_DRAW_SW_SUPPORT_ARGB8888         0
    #define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0
    #define LV_DRAW_SW_SUPPORT_L8               0
    #define LV_DRAW_SW_SUPPORT_AL88             0
    #define LV_DRAW_SW_SUPPORT_A8               1  /* needed for anti-aliased fonts */
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

/* Logging — off */
#define LV_USE_LOG      0
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

/* On ESP32, use abort() so a failed assertion produces a guru-meditation
 * backtrace in the serial monitor instead of a silent while(1) WDT loop. */
#define LV_ASSERT_HANDLER_INCLUDE <stdlib.h>
#define LV_ASSERT_HANDLER         abort();

/* Misc */
#define LV_SPRINTF_CUSTOM   0
#define LV_USE_FLOAT        1
#define LV_USE_MATRIX       0

/* Animation */
#define LV_USE_ANIM         1
#define LV_USE_TIMER        1
#define LV_USE_SYSMON       0
#define LV_USE_PROFILER     0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR  0

/* Widgets needed for dashboard */
#define LV_USE_ARC      0
#define LV_USE_LABEL    1
#define LV_USE_BTN      0
#define LV_USE_BTNMATRIX 0
#define LV_USE_BAR      1
#define LV_USE_SLIDER   0
#define LV_USE_TABLE    0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_ROLLER   0
#define LV_USE_TEXTAREA  0
#define LV_USE_CHART    0
#define LV_USE_CALENDAR 0
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN   0
#define LV_USE_KEYBOARD 0
#define LV_USE_LED      0
#define LV_USE_LIST     0
#define LV_USE_MENU     0
#define LV_USE_METER    0
#define LV_USE_MSGBOX   0
#define LV_USE_SPAN     0
#define LV_USE_SPINBOX  0
#define LV_USE_SPINNER  0
#define LV_USE_TABVIEW  0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN      0

/* Font */
#define LV_FONT_MONTSERRAT_8    1
#define LV_FONT_MONTSERRAT_10   0
#define LV_FONT_MONTSERRAT_12   1
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   0
#define LV_FONT_MONTSERRAT_18   0
#define LV_FONT_MONTSERRAT_20   0
#define LV_FONT_MONTSERRAT_22   0
#define LV_FONT_MONTSERRAT_24   0
#define LV_FONT_MONTSERRAT_26   0
#define LV_FONT_MONTSERRAT_28   0
#define LV_FONT_MONTSERRAT_30   0
#define LV_FONT_MONTSERRAT_32   0
#define LV_FONT_MONTSERRAT_34   0
#define LV_FONT_MONTSERRAT_36   0
#define LV_FONT_MONTSERRAT_38   0
#define LV_FONT_MONTSERRAT_40   0
#define LV_FONT_MONTSERRAT_42   0
#define LV_FONT_MONTSERRAT_44   0
#define LV_FONT_MONTSERRAT_46   0
#define LV_FONT_MONTSERRAT_48   0
#define LV_FONT_UNSCII_8        0
#define LV_FONT_UNSCII_16       0
#define LV_FONT_DEFAULT         &lv_font_montserrat_14

/* Image */
#define LV_USE_IMAGE    1
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_CANVAS   0
#define LV_USE_LINE     1  /* required by lv_scale (used internally by theme/bar) */
#define LV_USE_OBJ_ID   0

/* Theme */
#define LV_USE_THEME_DEFAULT    1
#if LV_USE_THEME_DEFAULT
    #define LV_THEME_DEFAULT_DARK 1
    #define LV_THEME_DEFAULT_GROW 0
    #define LV_THEME_DEFAULT_TRANSITION_TIME 80
#endif
#define LV_USE_THEME_SIMPLE     0
#define LV_USE_THEME_MONO       0

/* Layout */
#define LV_USE_FLEX     1
#define LV_USE_GRID     0

/* Drivers (not used — we wire them manually) */
#define LV_USE_SDL              0
#define LV_USE_LINUX_FBDEV      0
#define LV_USE_LINUX_DRM        0
#define LV_USE_OPENGLES         0
#define LV_USE_WINDOWS          0
#define LV_USE_EVDEV            0
#define LV_USE_LIBINPUT         0
#define LV_USE_X11              0
#define LV_USE_WAYLAND          0

/* LovyanGFX 1.2.x compatibility — these glyph format values exist in LovyanGFX's
 * bundled lv_font headers but were removed from LVGL 9.5.0. Defining them as macros
 * (not enum values) is valid for switch/case labels; none of these values overlap
 * with any LVGL 9.5.0 lv_font_glyph_format_t member. */
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
