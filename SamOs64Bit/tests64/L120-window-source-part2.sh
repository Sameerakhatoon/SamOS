#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# Title bar + 3 borders.
grep -q 'title_bar_graphics_info' "$ROOT/src/graphics/window.c" || fail "title bar missing"
grep -q 'border_left_graphics_info' "$ROOT/src/graphics/window.c" || fail "border_left missing"
grep -q 'border_right_graphics_info' "$ROOT/src/graphics/window.c" || fail "border_right missing"
grep -q 'border_bottom_graphics_info' "$ROOT/src/graphics/window.c" || fail "border_bottom missing"

# Terminals + transparency wiring.
grep -q 'window->title_bar_terminal = terminal_create' "$ROOT/src/graphics/window.c" \
    || fail "title bar terminal_create missing"
grep -q 'window->terminal = terminal_create' "$ROOT/src/graphics/window.c" \
    || fail "body terminal_create missing"
grep -q 'terminal_background_save(window->terminal)' "$ROOT/src/graphics/window.c" \
    || fail "background_save missing"
grep -q 'terminal_transparency_key_set(window->terminal' "$ROOT/src/graphics/window.c" \
    || fail "transparency_key set missing"

# Close icon math + draw.
grep -q 'close_btn.x' "$ROOT/src/graphics/window.c" || fail "close_btn.x missing"
grep -q 'window_draw_title_bar' "$ROOT/src/graphics/window.c" || fail "draw_title_bar call missing"

# Registration / focus / redraw.
grep -q 'vector_push(windows_vector, &window)' "$ROOT/src/graphics/window.c" \
    || fail "vector_push windows missing"
grep -q 'window_set_z_index(window' "$ROOT/src/graphics/window.c" \
    || fail "set_z_index missing"
grep -q 'window_focus(window)' "$ROOT/src/graphics/window.c" \
    || fail "focus missing"
grep -q 'graphics_redraw_all()' "$ROOT/src/graphics/window.c" \
    || fail "redraw_all missing"

# Function ends with `return window;`.
awk '/struct window\* window_create/,0' "$ROOT/src/graphics/window.c" \
    | grep -q 'return window;' \
    || fail "window_create missing return window;"

# Still source-only.
if grep -q 'build/graphics/window.o' "$ROOT/Makefile"; then
    fail "Makefile: window.o still must not be in FILES at L120"
fi

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L120 window source part 2"
