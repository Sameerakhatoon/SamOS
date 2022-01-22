#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

for sym in graphics_mouse_click graphics_mouse_click_handler \
           graphics_click_handler_set graphics_is_in_ignored_branch \
           graphics_get_child_at_position graphics_get_at_screen_position \
           grpahics_setup_stage_two; do
    grep -q "$sym" "$ROOT/src/graphics/graphics.c" || fail "graphics.c: $sym missing"
    grep -q "$sym" "$ROOT/src/graphics/graphics.h" || fail "graphics.h: $sym missing"
done

# mouse.h pulled into graphics.h.
grep -q '#include "mouse/mouse.h"' "$ROOT/src/graphics/graphics.h" \
    || fail "graphics.h: mouse.h include missing"
# window.h pulled into graphics.c.
grep -q '#include "graphics/window.h"' "$ROOT/src/graphics/graphics.c" \
    || fail "graphics.c: window.h include missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L141 graphics click handlers"
