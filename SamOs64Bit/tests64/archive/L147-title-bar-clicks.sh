#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q '^void window_title_bar_clicked' "$ROOT/src/graphics/window.c" \
    || fail "title bar click router missing"

# Close hit -> window_close.
awk '/^void window_title_bar_clicked/,/^}$/{print}' "$ROOT/src/graphics/window.c" > /tmp/tb.$$
grep -q 'window_close(win)' /tmp/tb.$$ || fail "close branch missing"
grep -q 'window_moving = win' /tmp/tb.$$ || fail "moving branch missing"
grep -q 'window_moving = NULL' /tmp/tb.$$ || fail "toggle-off branch missing"
rm -f /tmp/tb.$$

# Click handler installed on the title bar surface.
grep -q 'graphics_click_handler_set(title_bar_graphics_info, window_title_bar_clicked)' \
    "$ROOT/src/graphics/window.c" \
    || fail "title bar click handler not wired in window_create"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L147 title bar clicks"
