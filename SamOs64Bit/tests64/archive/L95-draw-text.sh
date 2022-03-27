#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q 'font = font_get_system_font' "$ROOT/src/graphics/font.c" \
    || fail "font.c: NULL-font fallback missing in font_draw_text"
grep -q 'font_system_init();' "$ROOT/src/kernel.c" \
    || fail "kernel.c: font_system_init call missing"
grep -q 'font_draw_text(graphics_screen_info(), NULL, 0, 0, "Hello world"' "$ROOT/src/kernel.c" \
    || fail "kernel.c: Hello world draw call missing"
grep -q '#include "graphics/font.h"' "$ROOT/src/kernel.c" \
    || fail "kernel.c: font.h include missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L95 draw_text"
