#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

for sym in window_keyboard_event_listener_on_event \
           window_keyboard_event_listener_on_event_keypress \
           window_keyboard_event_listener_on_event_capslock_change \
           window_keyboard_listener; do
    grep -q "$sym" "$ROOT/src/graphics/window.c" \
        || fail "window.c: $sym missing"
done

# Stage 2 init now wires the listener.
grep -q 'keyboard_register_handler(NULL, window_keyboard_listener)' \
    "$ROOT/src/graphics/window.c" \
    || fail "window.c: stage2 init missing keyboard_register_handler call"

# The on_event body routes via window_focused.
awk '/^void window_keyboard_event_listener_on_event\(/,/^}$/' \
    "$ROOT/src/graphics/window.c" | grep -q 'window_focused()' \
    || fail "window.c: on_event does not consult window_focused"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L175 window keyboard listener"
