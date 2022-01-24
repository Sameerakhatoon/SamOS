#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

for sym in window_get_from_graphics window_owns_graphics window_focused \
           window_redraw_region window_redraw_body_region window_title_set \
           window_get_largest_zindex window_recalculate_zindexes; do
    grep -q "$sym" "$ROOT/src/graphics/window.c" || fail "window.c: $sym missing"
done

for sym in window_get_from_graphics window_owns_graphics window_focused \
           window_redraw_region window_redraw_body_region window_title_set; do
    grep -q "$sym" "$ROOT/src/graphics/window.h" || fail "window.h: $sym missing"
done

# graphics_has_ancestor lands in graphics.c + .h.
grep -q '^bool graphics_has_ancestor' "$ROOT/src/graphics/graphics.c" \
    || fail "graphics.c: graphics_has_ancestor body missing"
grep -q 'graphics_has_ancestor' "$ROOT/src/graphics/graphics.h" \
    || fail "graphics.h: graphics_has_ancestor decl missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L145 window helpers"
