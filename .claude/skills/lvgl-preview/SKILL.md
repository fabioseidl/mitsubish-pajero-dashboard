---
name: lvgl-preview
description: Designing display-client screens as HTML prototypes and turning them into hand-written LVGL code, verified pixel-by-pixel in a native simulator — render_lvgl.sh, render_html.sh, compare.py, the SDL window, fixtures, and the HTML→LVGL CSS subset. Use when designing or changing a client screen, converting a prototype to LVGL, checking how a UI change looks without hardware, or adding the simulator to another client project.
---

# LVGL preview: HTML prototype → LVGL → simulator → compare

Client UIs are designed as an HTML page, implemented as hand-written LVGL code,
and verified by rendering both and diffing them. No SquareLine, and no flashing
needed to see a layout change.

```
sim/prototype.html ──render_html.sh──► html.png ┐
                                                ├─ compare.py ─► compare.png + numbers
src/<x>_ui.cpp ──env:sim──render_lvgl.sh──► lvgl.png ┘
        ▲ same sim/fixtures/<state>.txt drives both
```

Projects with the simulator: **main_hud** (`HudUi`), **main_display**
(`dashboard_view` + `app_ui`). Others: see *Adding a project*.

## The loop

```bash
S=.claude/skills/lvgl-preview/scripts
$S/preview.sh <project>             # every fixture; ~15 s for main_hud
$S/preview.sh <project> <fixture>   # one fixture
```

Writes `projects/<project>/.pio/preview/<fixture>.{html,lvgl,compare}.png` and
prints, per fixture:

```
similarity   98.41%      share of pixels within --tol
diff bbox    (x0,y0,x1,y1)   where they disagree
html ink     (…)         bbox of everything non-background in each render —
lvgl ink     (…)         compare these to read an offset as a number
```

Then **Read the `compare.png`** (HTML | LVGL | diff). In the diff, red = ink only
in HTML, cyan = ink only in LVGL, yellow = same brightness, different colour. A
red edge on one side and cyan on the other is an offset; the colours tell you
which way.

Zoom into a region to measure small elements:

```bash
python3 $S/compare.py html.png lvgl.png out.png --crop x,y,w,h --zoom 3
```

`--crop` makes the ink bboxes relative to the crop, which is the quickest way to
measure one widget.

**Which side is wrong?** For a new design, the HTML is the spec: fix the LVGL. For
an existing screen being captured as a prototype, the firmware is the truth: fix
the HTML (`main_hud`'s speed digits needed `top: 20px` in CSS to land at LVGL's
y=16, for example).

**When to stop:** remaining differences of ≤1 px and anti-aliasing fringes are
noise between FreeType (`lv_font_conv`) and Chrome's rasteriser — don't chase
them. Above ~98% similarity with ink bboxes within 1 px is a match. A text
block 2+ px off, a wrong colour, or a missing/extra element is a real bug.

## Interactive window (for the user)

```bash
pio run -d projects/<project> -e sim_sdl
projects/<project>/.pio/build/sim_sdl/program --fixture projects/<project>/sim/fixtures/<state>.txt --window
```

Mouse = touch, `R` reloads the fixture, Esc quits, `--zoom N` scales the window.
main_hud: ↑/↓ speed ±5, `O` online, `B` buttons (default zoom 2).
main_display: ↑/↓ speed ±5, ←/→ rpm ±100, `L` backlight step, `O` online (zoom 1).

Needs `brew install sdl2`; the headless `sim` env has no dependencies.

## Rules for the HTML prototype

The prototype must only use what LVGL can draw, or the conversion is guesswork.

- **Fixed canvas**: a `#screen` div at the panel's exact px size, and
  `<meta name="lvgl-screen" content="WxH">` (render_html.sh reads it). Absolute
  px everywhere; no `%`, `vw`, `em` for geometry.
- **CSS that maps to LVGL** — use: `position:absolute`, flex (→ `LV_LAYOUT_FLEX`),
  `background` solid or a two-stop linear gradient (→ `bg_grad`), `border`,
  `border-radius`, `opacity`, `outline` (→ `outline_*`), images. A single
  `box-shadow` is possible but costly on these panels — prefer none.
- **Never**: `filter`, `backdrop-filter`, `transform`, multiple shadows,
  `text-shadow`, blend modes, web-font weights/sizes that aren't converted.
- **Fonts**: list every font+size in the header comment. Each is one
  `lv_font_conv` file in flash; reuse sizes before adding one, and restrict glyph
  ranges (`-r`) for big numeral faces. `@font-face` points at the repo `.ttf`
  the LVGL font was generated from.
- **Colours** as CSS variables on `:root`, written the way `lv_color_hex()`
  takes them. RGB565 on the panel quantises them; the simulator shows that.
- **State from the query string** with the same keys as the fixtures, so one
  fixture drives both renders.

## Rules for the LVGL side

- The view is a **pure-LVGL class** (`HudUi` in `main_hud`): builds widgets,
  exposes setters (`setSpeed`, …), no Arduino/panel/touch headers. It compiles
  for the board and the simulator. The board's `IScreenController` owns
  hardware, timing and the payload, and calls the view's setters from `tick()`.
- Hand-written, not generator-shaped: named constants for geometry, one
  `makeX()` helper per repeated widget, a comment on any offset that exists
  to match the design.
- Re-align auto-sized labels after `set_text` (LVGL doesn't re-run `align`).
- The default dark theme is on in `lv_conf.h`; set every style the design
  specifies explicitly rather than relying on theme values.

## Fixtures

`projects/<project>/sim/fixtures/<state>.txt`, `key=value` lines, `#` comments.
Keys are whatever the project's `sim_main.cpp` understands (values may contain
spaces; render_html.sh URL-encodes them):

- main_hud: `speed`, `online`, `brightness`, `buttons`.
- main_display: Payload fields (`speed`, `rpm`, `gear`, `target_gear`,
  `fuel_rate`, `consumption`, `avg_consumption`, `distance`, `trip_time_s`,
  `boost`, `load`, `coolant`, `voltage`, `ax`/`ay`/`az`, …), client sensors
  (`gps_time`, `altitude`, `course`, `env_temp`, `humidity`, `nan` = no
  reading), `online`, `brightness` (steps the 10-level cycle; boot is 50%, which
  dims the whole frame) and `elapsed_ms` (runs LVGL timers — ≥3000 hides the
  backlight readout). Cover at least: normal driving, the
widest text each label can show, and offline. Unknown keys print a warning.

## Porting a SquareLine screen

main_display was ported this way; the same applies to any remaining export.

1. Build `env:sim` against the export as-is (`+<ui/*.c>`; clang needs
   `-Wno-error=implicit-function-declaration -Wl,-undefined,dynamic_lookup` for
   `ui_helpers.c`) and render every fixture to a baseline folder.
2. Hand-write the view: keep the export's `LV_ALIGN_CENTER` offsets (labels
   re-centre as their text changes), its creation order (z-order), and drop
   widgets the export permanently `LV_OBJ_FLAG_HIDDEN`s.
3. `compare.py baseline.png port.png out.png --tol 0` must report **100.00%** —
   both sides are LVGL, so any pixel difference is a porting mistake.
4. Fonts and images stay as the converted `.c` files; declare them inside
   `extern "C" { }` from C++, and compile only the ones the view uses.

## Prototype geometry for LV_ALIGN_CENTER

A zero-size flex anchor reproduces it without `transform` (main_display's
`.c` class): `left: calc(512px + var(--x) * 1px)`, `width: 0`, `display: flex`,
`justify-content: center`. Set `line-height` to the lv_font `line_height` and
nudge each font role so the CSS baseline lands on LVGL's
(box top + `line_height` − `base_line`) — measured offsets for Roboto are
−4 px at 120, −3 at 60, −2 at 50, −1 at 28/20, 0 for Montserrat 16.

## What the simulator does not tell you

- **Speed.** A Mac renders anything instantly. For FPS/CPU on the ESP32 enable
  `LV_USE_SYSMON` + `LV_USE_PERF_MONITOR` in a debug build on the board.
- **The physical panel** — gamma, viewing angle, sunlight, QSPI tearing.
- **Touch feel** — main_hud's 80 ms hold window and wake-only tap live in the
  board controller, not the view.

## Adding a project

1. Split the view out of the screen controller into `include/<x>_ui.h` /
   `src/<x>_ui.cpp` (pure LVGL). Build the firmware to confirm nothing changed.
2. `platformio.ini`: `[platformio] default_envs = <firmware env>`, a shared
   `[lvgl] dep = lvgl/lvgl @ <exact version>` used by **both** firmware and sim
   (a version gap breaks the pixel match), and `env:sim` / `env:sim_sdl` — copy
   main_hud's, adjusting `build_src_filter` to the view and font files.
3. `sim/sim_main.cpp` — copy main_hud's: memory framebuffer flush, fake tick,
   fixture parser, PPM out, optional SDL window. Screen size from `pin_config.h`.
4. `sim/fixtures/*.txt` and `sim/prototype.html` (with the `lvgl-screen` meta).
5. Run `preview.sh <project>` and bring it to a match.

If the project's UI is a SquareLine export, port it first (see *Porting a
SquareLine screen*). glass_display and client_simple_hud are not done yet.
