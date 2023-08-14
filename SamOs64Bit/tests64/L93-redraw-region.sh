#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q 'graphics_redraw_region' "$ROOT/src/graphics/graphics.h" \
    || fail "graphics.h: redraw_region prototype missing"
grep -q 'void graphics_redraw_region(' "$ROOT/src/graphics/graphics.c" \
    || fail "graphics.c: redraw_region definition missing"
grep -q 'graphics_redraw_graphics_to_screen' "$ROOT/src/graphics/graphics.c" \
    || fail "graphics.c: redraw_graphics_to_screen missing"
grep -q 'graphics_redraw_children' "$ROOT/src/graphics/graphics.c" \
    || fail "graphics.c: redraw_children missing"

# Children walk hooked into redraw.
awk '/void graphics_redraw\(struct graphics_info\* g\){/,/^}$/{print}' \
    "$ROOT/src/graphics/graphics.c" | grep -q 'graphics_redraw_children' \
    || fail "graphics.c: graphics_redraw must call graphics_redraw_children"

# MIN/MAX in kernel.h.
grep -q '#define MIN' "$ROOT/src/kernel.h" || fail "kernel.h: MIN missing"
grep -q '#define MAX' "$ROOT/src/kernel.h" || fail "kernel.h: MAX missing"

# graphics_paste_pixels_to_framebuffer no longer file-static.
if grep -q 'static void graphics_paste_pixels_to_framebuffer' "$ROOT/src/graphics/graphics.c"; then
    fail "graphics.c: paste helper still static, must be file-scope for L93"
fi

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L93 redraw_region"
