#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q 'window_position_set(struct window\* window, size_t new_x, size_t new_y)' \
    "$ROOT/src/graphics/window.h" || fail "window.h: prototype missing"
grep -q '^int window_position_set' "$ROOT/src/graphics/window.c" \
    || fail "window.c: window_position_set body missing"

# Weak stubs for L135/L136 forward refs.
grep -q 'graphics_info_recalculate' "$ROOT/src/graphics/window.c" \
    || fail "window.c: graphics_info_recalculate stub missing"
grep -q 'window_redraw' "$ROOT/src/graphics/window.c" \
    || fail "window.c: window_redraw stub missing"

# Bounds clamping.
grep -q 'screen->width  - window->width' "$ROOT/src/graphics/window.c" \
    || fail "window.c: x bound clamp missing"
grep -q 'screen->height - window->height' "$ROOT/src/graphics/window.c" \
    || fail "window.c: y bound clamp missing"

# bring_to_top after move.
awk '/^int window_position_set/,/^}$/{print}' "$ROOT/src/graphics/window.c" \
    | grep -q 'window_bring_to_top(window)' \
    || fail "window.c: window_bring_to_top call missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L134 window_position_set"
