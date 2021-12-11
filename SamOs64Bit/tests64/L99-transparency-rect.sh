#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# graphics_draw_rect now has a real body.
grep -q 'graphics_draw_rect(graphics_info, lx, ly, pixel_color)' "$ROOT/src/graphics/graphics.c" \
    && fail "graphics.c: rect body must walk pixels, not recurse"
grep -q 'graphics_draw_pixel(graphics_info, lx, ly, pixel_color)' "$ROOT/src/graphics/graphics.c" \
    || fail "graphics.c: rect must walk per-pixel"

for sym in graphics_ignore_color graphics_ignore_color_finish \
           graphics_transparency_key_set graphics_transparency_key_remove; do
    grep -q "$sym" "$ROOT/src/graphics/graphics.h" \
        || fail "graphics.h: $sym not declared"
    grep -q "$sym" "$ROOT/src/graphics/graphics.c" \
        || fail "graphics.c: $sym not implemented"
done

# Terminal stubs route to the graphics layer now.
grep -q 'graphics_transparency_key_set(terminal->graphics_info' "$ROOT/src/graphics/terminal.c" \
    || fail "terminal.c: transparency_key_set still a stub"
grep -q 'graphics_ignore_color(terminal->graphics_info' "$ROOT/src/graphics/terminal.c" \
    || fail "terminal.c: ignore_color still a stub"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L99 transparency + rect"
