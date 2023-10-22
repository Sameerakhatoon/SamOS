#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

awk '/^void window_screen_mouse_move_handler/,/^}$/{print}' "$ROOT/src/graphics/window.c" \
    > /tmp/m.$$
# Body wraps everything inside the if(window_moving) guard (L148 bugfix).
grep -q 'if(window_moving){' /tmp/m.$$ || fail "no if(window_moving) guard"
grep -q 'window_position_set(window_moving' /tmp/m.$$ \
    || fail "window_position_set call missing"
grep -q 'WINDOW_EVENT_TYPE_MOUSE_MOVE' /tmp/m.$$ \
    || fail "MOUSE_MOVE event missing"
# event_push must be inside the if-block.
n_braces_before_push=$(awk '
    /^void window_screen_mouse_move_handler/ {start=1; next}
    !start {next}
    /if\(window_moving\){/ {in_if=1; next}
    in_if && /window_event_push/ {print "INSIDE"; exit}
    in_if && /^    }/ {print "OUTSIDE"; exit}
' /tmp/m.$$)
[ "$n_braces_before_push" = "INSIDE" ] \
    || fail "L148 bugfix: window_event_push must live inside if(window_moving)"
rm -f /tmp/m.$$

grep -q 'window_title_bar_mouse_moved' "$ROOT/src/graphics/window.c" \
    || fail "title bar move stub missing"
grep -q 'graphics_move_handler_set(title_bar_graphics_info, window_title_bar_mouse_moved)' \
    "$ROOT/src/graphics/window.c" \
    || fail "title bar move handler not wired"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L148 move with mouse + bugfix"
