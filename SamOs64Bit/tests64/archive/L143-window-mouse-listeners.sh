#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

for sym in window_screen_mouse_move_handler window_get_at_position window_click_handler; do
    grep -q "$sym" "$ROOT/src/graphics/window.c" || fail "window.c: $sym missing"
done

awk '/^int window_system_initialize_stage2/,/^}$/{print}' "$ROOT/src/graphics/window.c" \
    > /tmp/init2.$$
grep -q 'mouse_register_move_handler(NULL,  window_screen_mouse_move_handler)' /tmp/init2.$$ \
    || fail "stage2: move register missing"
grep -q 'mouse_register_click_handler(NULL, window_click_handler)' /tmp/init2.$$ \
    || fail "stage2: click register missing"
rm -f /tmp/init2.$$

# window_click stub keeps the link clean until L144.
grep -q 'window_click(struct window\* window' "$ROOT/src/graphics/window.c" \
    || fail "window_click stub missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L143 window mouse listeners"
