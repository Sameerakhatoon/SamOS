#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

for sym in font_system_init font_load font_get_loaded_font font_get_system_font \
           font_create font_draw font_draw_text; do
    grep -q "$sym" "$ROOT/src/graphics/font.h" \
        || fail "font.h: $sym not declared"
    grep -q "$sym" "$ROOT/src/graphics/font.c" \
        || fail "font.c: $sym not implemented"
done

grep -q 'font_draw_from_index' "$ROOT/src/graphics/font.c" \
    || fail "font.c: font_draw_from_index missing"
grep -q 'graphics_redraw_graphics_to_screen' "$ROOT/src/graphics/font.c" \
    || fail "font.c: per-glyph redraw call missing"

# font.o NOW must be in build.
grep -q 'build/graphics/font.o' "$ROOT/Makefile" \
    || fail "Makefile: font.o not in FILES"

# graphics.h exports the redraw helper for font.c.
grep -q 'graphics_redraw_graphics_to_screen' "$ROOT/src/graphics/graphics.h" \
    || fail "graphics.h: missing redraw_graphics_to_screen prototype"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L94 font part 2"
