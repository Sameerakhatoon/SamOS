#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# graphics_has_ancestor decl and body exist.
grep -q 'graphics_has_ancestor' "$ROOT/src/graphics/graphics.h" \
    || fail "graphics.h: graphics_has_ancestor missing"
grep -q '^bool graphics_has_ancestor' "$ROOT/src/graphics/graphics.c" \
    || fail "graphics.c: graphics_has_ancestor body missing"

# Corrected graphics_move_handler_set decl (no stray comma).
grep -q 'GRAPHICS_MOUSE_MOVE_FUNCTION move_function' "$ROOT/src/graphics/graphics.h" \
    || fail "graphics.h: move_handler_set decl wrong"
# No duplicate stub of graphics_mouse_click_handler.
count=$(grep -c '^void graphics_mouse_click_handler' "$ROOT/src/graphics/graphics.c")
[ "$count" = "1" ] || fail "graphics.c: duplicate graphics_mouse_click_handler (found $count)"

# window.h exposes the L145 helpers and L149 window_event_push.
for sym in window_owns_graphics window_focused window_get_from_graphics \
           window_redraw_region window_redraw_body_region window_title_set \
           window_event_push; do
    grep -q "$sym" "$ROOT/src/graphics/window.h" || fail "window.h: $sym missing"
done

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L149 fix compile errors"
