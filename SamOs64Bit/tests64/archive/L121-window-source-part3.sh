#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

for sym in window_draw_title_bar window_reorder window_set_z_index \
           window_unfocus window_focus; do
    grep -q "^void $sym\|^int $sym" "$ROOT/src/graphics/window.c" \
        || fail "window.c: $sym definition missing"
done

# Close icon ignore_color path.
grep -q 'terminal_ignore_color(window->title_bar_terminal' "$ROOT/src/graphics/window.c" \
    || fail "ignore_color call missing"

# z-index touches both graphics and vector.
grep -q 'graphics_set_z_index(window->root_graphics' "$ROOT/src/graphics/window.c" \
    || fail "z_index graphics call missing"
grep -q 'vector_reorder(windows_vector' "$ROOT/src/graphics/window.c" \
    || fail "z_index vector_reorder missing"

# focus paints red, unfocus paints black.
grep -q 'window_draw_title_bar(window, red)' "$ROOT/src/graphics/window.c" \
    || fail "focus red repaint missing"
grep -q 'window_draw_title_bar(old_focused_window, black)' "$ROOT/src/graphics/window.c" \
    || fail "unfocus black repaint missing"

# Still source-only.
if grep -q 'build/graphics/window.o' "$ROOT/Makefile"; then
    fail "Makefile: window.o still must not be in FILES at L121"
fi

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L121 window source part 3"
