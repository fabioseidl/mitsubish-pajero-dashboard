#!/usr/bin/env bash
# Render every fixture (or one) of a project through HTML and LVGL and compare.
# Usage: preview.sh <project> [fixture-name]
# Output: projects/<project>/.pio/preview/<fixture>.{html,lvgl,compare}.png
set -euo pipefail

project="$1"; only="${2:-}"
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(git -C "$here" rev-parse --show-toplevel)"
fixtures="$repo/projects/$project/sim/fixtures"
out="$repo/projects/$project/.pio/preview"
mkdir -p "$out"

for f in "$fixtures"/*.txt; do
    name="$(basename "$f" .txt)"
    [ -z "$only" ] || [ "$name" = "$only" ] || continue
    echo "── $name"
    "$here/render_html.sh" "$project" "$f" "$out/$name.html.png" >/dev/null
    "$here/render_lvgl.sh" "$project" "$f" "$out/$name.lvgl.png" >/dev/null
    python3 "$here/compare.py" "$out/$name.html.png" "$out/$name.lvgl.png" "$out/$name.compare.png"
done
