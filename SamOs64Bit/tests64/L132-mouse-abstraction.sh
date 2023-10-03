#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

[ -f "$ROOT/src/mouse/mouse.h" ] || fail "mouse.h missing"
[ -f "$ROOT/src/mouse/mouse.c" ] || fail "mouse.c missing"

for sym in MOUSE_GRAPHIC_DEFAULT_WIDTH MOUSE_GRAPHIC_DEFAULT_HEIGHT \
           MOUSE_GRAPHIC_ZINDEX MOUSE_NO_CLICK MOUSE_LEFT_BUTTON_CLICKED \
           MOUSE_RIGHT_BUTTON_CLICKED MOUSE_MIDDLE_BUTTON_CLICKED \
           MOUSE_CLICK_TYPE MOUSE_INIT_FUNCTION MOUSE_DRAW_FUNCTION \
           MOUSE_CLICK_EVENT_HANDLER_FUNCTION MOUSE_MOVE_EVENT_HANDLER_FUNCTION; do
    grep -q "$sym" "$ROOT/src/mouse/mouse.h" || fail "mouse.h: $sym missing"
done
grep -q 'struct mouse {' "$ROOT/src/mouse/mouse.h" || fail "struct mouse missing"

for sym in mouse_system_init mouse_system_load_static_drivers \
           mouse_draw_default_impl mouse_register mouse_position_set \
           mouse_click mouse_moved; do
    grep -q "$sym" "$ROOT/src/mouse/mouse.c" || fail "mouse.c: $sym missing"
done

# mouse.o must NOT yet be in build.
if grep -q 'build/mouse/mouse.o' "$ROOT/Makefile"; then
    fail "Makefile: mouse.o should not yet be in FILES at L132"
fi

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L132 mouse abstraction"
