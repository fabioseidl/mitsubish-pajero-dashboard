#!/usr/bin/env bash
# Render one fixture through a project's native LVGL simulator to a PNG.
# Usage: render_lvgl.sh <project> <fixture.txt> <out.png>
set -euo pipefail

project="$1"; fixture="$2"; out="$3"
repo="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
dir="$repo/projects/$project"
pio="$(command -v pio || echo "$HOME/.platformio/penv/bin/pio")"

# Silent unless the build fails; then show the real compiler output.
if ! log="$("$pio" run -d "$dir" -e sim 2>&1)"; then
    echo "$log" | grep -E "error|Error" | head -40 >&2
    exit 1
fi

ppm="$(mktemp -t lvgl).ppm"
"$dir/.pio/build/sim/program" --fixture "$fixture" --out "$ppm"
python3 -c 'import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])' "$ppm" "$out"
rm -f "$ppm"
echo "$out"
