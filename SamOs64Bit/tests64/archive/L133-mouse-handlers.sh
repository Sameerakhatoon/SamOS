#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

for sym in mouse_draw mouse_register_click_handler mouse_register_move_handler \
           mouse_unregister_click_handler mouse_unregister_move_handler; do
    grep -q "$sym" "$ROOT/src/mouse/mouse.h" || fail "mouse.h: $sym missing"
    grep -q "$sym" "$ROOT/src/mouse/mouse.c" || fail "mouse.c: $sym missing"
done

# NULL-mouse recursion path.
grep -q 'mouse_driver_vector' "$ROOT/src/mouse/mouse.c" || fail "driver-vector recursion missing"
grep -q 'vector_pop_element(mouse->event_handlers.move_handlers' "$ROOT/src/mouse/mouse.c" \
    || fail "move pop missing"
grep -q 'vector_pop_element(mouse->event_handlers.click_handlers' "$ROOT/src/mouse/mouse.c" \
    || fail "click pop missing"

if grep -q 'build/mouse/mouse.o' "$ROOT/Makefile"; then
    fail "Makefile: mouse.o should still not be in FILES at L133"
fi

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L133 mouse handlers"
