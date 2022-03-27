#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

for sym in 'struct keyboard_event' 'struct keyboard_listener' \
           KEYBOARD_EVENT_KEY_PRESS KEYBOARD_EVENT_CAPS_LOCK_CHANGE \
           keyboard_default keyboard_register_handler \
           keyboard_unregister_handler keyboard_get_listener_ptr \
           keyboard_push_event_to_listeners key_listeners; do
    grep -q "$sym" "$ROOT/src/keyboard/keyboard.h" \
        || fail "keyboard.h: $sym missing"
done

for sym in keyboard_default keyboard_register_handler \
           keyboard_unregister_handler keyboard_get_listener_ptr \
           keyboard_push_event_to_listeners; do
    grep -q "$sym" "$ROOT/src/keyboard/keyboard.c" \
        || fail "keyboard.c: $sym body missing"
done

# Insert path allocates the vector.
grep -q 'keyboard->key_listeners =' "$ROOT/src/keyboard/keyboard.c" \
    || fail "keyboard.c: listener vector not allocated in insert"
# push() fires listeners.
grep -q 'keyboard_push_event_to_listeners(keyboard_default()' "$ROOT/src/keyboard/keyboard.c" \
    || fail "keyboard.c: push() does not fan out"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L174 keyboard events"
