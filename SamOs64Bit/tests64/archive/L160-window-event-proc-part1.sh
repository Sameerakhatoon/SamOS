#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# struct window_event_userland on window.h.
grep -q 'struct window_event_userland' "$ROOT/src/graphics/window.h" \
    || fail "window.h: window_event_userland missing"
grep -q '^void\s\+window_event_to_userland' "$ROOT/src/graphics/window.h" \
    || fail "window.h: window_event_to_userland decl missing"
grep -q '^void window_event_to_userland' "$ROOT/src/graphics/window.c" \
    || fail "window.c: window_event_to_userland body missing"

# vector_grow + vector_has.
for sym in vector_grow vector_has; do
    grep -q "$sym" "$ROOT/src/lib/vector/vector.h" || fail "vector.h: $sym decl missing"
    grep -q "^int $sym" "$ROOT/src/lib/vector/vector.c" || fail "vector.c: $sym body missing"
done

# Process gets PROCESS_MAX_WINDOW_EVENTS_RECORDED and window_events block.
grep -q 'PROCESS_MAX_WINDOW_EVENTS_RECORDED 1000' "$ROOT/src/task/process.h" \
    || fail "process.h: ring ceiling macro missing"
grep -q 'window_events' "$ROOT/src/task/process.h" \
    || fail "process.h: window_events field missing"
grep -q 'process->window_events.vector =' "$ROOT/src/task/process.c" \
    || fail "process.c: window_events init missing"
grep -q 'vector_grow(process->window_events.vector' "$ROOT/src/task/process.c" \
    || fail "process.c: vector_grow not called"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L160 window event proc part 1"
