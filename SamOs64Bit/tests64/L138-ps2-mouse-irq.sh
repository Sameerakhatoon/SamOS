#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q 'void ps2_mouse_handle_interrupt(struct interrupt_frame\* frame)' \
    "$ROOT/src/mouse/ps2mouse.c" || fail "handler body missing"
grep -q 'mouse_position_set(&ps2_mouse' "$ROOT/src/mouse/ps2mouse.c" \
    || fail "position_set call missing"
grep -q 'mouse_click(&ps2_mouse' "$ROOT/src/mouse/ps2mouse.c" \
    || fail "mouse_click call missing"
grep -q 'mouse_moved(&ps2_mouse)' "$ROOT/src/mouse/ps2mouse.c" \
    || fail "mouse_moved call missing"

# window_terminal helper landed.
grep -q '^struct terminal\* window_terminal' "$ROOT/src/graphics/window.c" \
    || fail "window.c: window_terminal body missing"
grep -q 'window_terminal' "$ROOT/src/graphics/window.h" \
    || fail "window.h: window_terminal decl missing"

# mouse.o + ps2mouse.o in build.
grep -q 'build/mouse/mouse.o' "$ROOT/Makefile" || fail "Makefile: mouse.o not in FILES"
grep -q 'build/mouse/ps2mouse.o' "$ROOT/Makefile" || fail "Makefile: ps2mouse.o not in FILES"
grep -q 'build/mouse' "$ROOT/build.sh" || fail "build.sh: build/mouse missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L138 PS/2 mouse IRQ + link"
