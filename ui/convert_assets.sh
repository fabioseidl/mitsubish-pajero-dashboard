#!/usr/bin/env bash
# Regenerates every converted LVGL font and image in the client projects from
# the source assets under ui/<project>/assets. Run from anywhere; overwrites the
# ui_font_*.c / ui_img_*.c files in projects/*/src.
#
# Needs: node (npx fetches lv_font_conv), python3 with pypng and lz4
# (pip install pypng lz4), and one PlatformIO build of an LVGL project so
# LVGLImage.py is on disk.
set -euo pipefail

repo="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
cd "$repo"

font_conv=(npx -y lv_font_conv@1.5.3)
lvgl_image="$(ls projects/*/.pio/libdeps/*/lvgl/scripts/LVGLImage.py 2>/dev/null | head -1)"
if [ -z "$lvgl_image" ]; then
    echo "LVGLImage.py not found: build any LVGL project first (pio run -d projects/main_hud -e sim)" >&2
    exit 1
fi

# font <ttf> <size> <out.c> <lv_font_conv range/option args...>; the symbol is the file's basename.
font() {
    local ttf="$1" size="$2" out="$3"; shift 3
    "${font_conv[@]}" --font "$ttf" --size "$size" "$@" --bpp 4 --no-compress --format lvgl \
        --lv-include lvgl.h -o "$out"
}

# image <png> <colour format> <out.c>; RGB565 for opaque images, RGB565A8 with alpha.
image() {
    local png="$1" cf="$2" out="$3" tmp
    tmp="$(mktemp -d)"
    python3 "$lvgl_image" --ofmt C --cf "$cf" --name "$(basename "$out" .c)" -o "$tmp" "$png" >/dev/null
    mv "$tmp/$(basename "$out")" "$out"
    rmdir "$tmp"
}

# Speed digits only (and "-") for the big numeral faces; full printable ASCII elsewhere.
ROBOTO_BOLD=ui/client_simple_hud/assets/Roboto-Bold.ttf
ROBOTO_REGULAR=ui/client_simple_hud/assets/Roboto-Regular.ttf

font "$ROBOTO_BOLD" 260 projects/main_hud/src/ui_font_roboto_bold_260.c -r 0x2D -r 0x30-0x39
font "$ROBOTO_BOLD" 28  projects/main_hud/src/ui_font_roboto_bold_28.c  -r 0x20-0x7f
font "$ROBOTO_BOLD" 56  projects/glass_display/src/ui_font_roboto_bold_56.c -r 0x2D -r 0x30-0x39

font "$ROBOTO_BOLD"    150 projects/client_simple_hud/src/ui_font_roboto_bold_150.c   -r 0x20-0x7f --no-prefilter
font "$ROBOTO_REGULAR" 24  projects/client_simple_hud/src/ui_font_roboto_regular_24.c -r 0x20-0x7f --no-prefilter
image ui/client_simple_hud/assets/mitsubishi_logo.png RGB565A8 projects/client_simple_hud/src/ui_img_mitsubishi_logo.c
image ui/client_simple_hud/assets/icon_server.png     RGB565A8 projects/client_simple_hud/src/ui_img_icon_server.c

MD=ui/main_display/assets
font "$MD/Roboto-Bold.ttf"    120 projects/main_display/src/ui_font_roboto_bold_120.c   -r 0x20-0x7f --no-prefilter
font "$MD/Roboto-Bold.ttf"    60  projects/main_display/src/ui_font_roboto_bold_60.c    -r 0x20-0x7f --no-prefilter
font "$MD/Roboto-Regular.ttf" 50  projects/main_display/src/ui_font_roboto_regular_50.c -r 0x20-0x7f --no-prefilter
font "$MD/Roboto-Regular.ttf" 28  projects/main_display/src/ui_font_roboto_regular_28.c -r 0x20-0x7f --no-prefilter
font "$MD/Roboto-Regular.ttf" 20  projects/main_display/src/ui_font_roboto_regular_20.c -r 0x20-0x7f --no-prefilter
# Material Design Icons (Pictogrammers Free License): only the glyphs the dashboard draws.
# 24 px: header pills and tile icons; 16 px: the vent-mode arrow.
MDI_24=0xF050F,0xF0717,0xF0210,0xF0D61,0xF0482,0xF006A,0xF0731,0xF0734,0xF01FA,0xF0C4A,0xF0C4C
MDI_24=$MDI_24,0xF0B6B,0xF029A,0xF07B1,0xF0298,0xF0462,0xF051B,0xF03C8,0xF0D43,0xF010C,0xF0238,0xF02F5
font "$MD/MaterialDesignIcons.ttf" 24 projects/main_display/src/ui_font_mdi_24.c -r "$MDI_24"
font "$MD/MaterialDesignIcons.ttf" 16 projects/main_display/src/ui_font_mdi_16.c -r 0xF0731
image "$MD/background.png"       RGB565   projects/main_display/src/ui_img_background.c
image "$MD/icon_temperature.png" RGB565A8 projects/main_display/src/ui_img_icon_temperature.c
