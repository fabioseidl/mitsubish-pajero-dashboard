#!/usr/bin/env bash
# Screenshot a project's HTML prototype at the exact panel size for one fixture.
# Usage: render_html.sh <project> <fixture.txt> <out.png>
# Panel size comes from <meta name="lvgl-screen" content="WxH"> in the prototype.
set -euo pipefail

project="$1"; fixture="$2"; out="$3"
repo="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
html="$repo/projects/$project/sim/prototype.html"
chrome="${CHROME:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"

size="$(sed -n 's/.*name="lvgl-screen" content="\([0-9]*x[0-9]*\)".*/\1/p' "$html")"
[ -n "$size" ] || { echo "no lvgl-screen meta in $html" >&2; exit 1; }
w="${size%x*}"; h="${size#*x}"

# Fixture key=value lines become the query string the prototype reads; values are
# URL-encoded so text such as "2026-10-16 14:35:00" survives intact.
query="$(python3 -c '
import sys, urllib.parse
pairs = []
for line in open(sys.argv[1]):
    line = line.rstrip("\r\n")
    if line.strip().startswith("#") or "=" not in line:
        continue
    k, v = line.split("=", 1)
    pairs.append((k.strip(), v))
print(urllib.parse.urlencode(pairs))' "$fixture")"

# --allow-file-access-from-files lets @font-face load the repo's .ttf over file://.
"$chrome" --headless=new --disable-gpu --hide-scrollbars \
    --force-device-scale-factor=1 --default-background-color=000000ff \
    --allow-file-access-from-files --virtual-time-budget=3000 \
    --window-size="$w,$h" --screenshot="$out" "file://$html?$query" >/dev/null 2>&1

# Chrome may add window chrome rows in some versions; crop to the panel.
python3 -c 'import sys; from PIL import Image; w,h=int(sys.argv[2]),int(sys.argv[3]); im=Image.open(sys.argv[1]).convert("RGB"); im.crop((0,0,w,h)).save(sys.argv[1])' "$out" "$w" "$h"
echo "$out"
