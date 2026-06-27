# Pre-build cleanup for the main_display project.
#
# The SquareLine UI is exported into a cloud-synced folder, so every export can
# leave "conflict copy" duplicates under src/ (e.g. "ui_themes 2.c",
# "ui_main 3.c"). They are byte-identical to the canonical files, so compiling
# them produces duplicate-symbol link errors. PlatformIO's build_src_filter
# can't exclude them because the filename contains a space (the filter splits on
# whitespace), so we delete them here before each build.
#
# Only files matching "<name> <digit>.<ext>" are removed — i.e. a space then a
# single digit just before the extension. Canonical files like
# "ui_font_robotoregular20.c" have no space before the digit and are untouched.

import glob
import os
import re

Import("env")  # noqa: F821  (injected by PlatformIO)

SRC_DIR = os.path.join(env.subst("$PROJECT_DIR"), "src")  # noqa: F821
PATTERN = re.compile(r" \d+\.[A-Za-z]+$")

removed = 0
for path in glob.glob(os.path.join(SRC_DIR, "**", "* [0-9]*.*"), recursive=True):
    if PATTERN.search(os.path.basename(path)):
        try:
            os.remove(path)
            removed += 1
        except OSError:
            pass

if removed:
    print("[clean_dupes] removed %d cloud-sync conflict copy(ies) from src/" % removed)
