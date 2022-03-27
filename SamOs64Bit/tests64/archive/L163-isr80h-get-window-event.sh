#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q 'SYSTEM_COMMAND18_GET_WINDOW_EVENT' "$ROOT/src/isr80h/isr80h.h" \
    || fail "isr80h.h: command 18 missing"
grep -q 'SYSTEM_COMMAND18_GET_WINDOW_EVENT' "$ROOT/src/isr80h/isr80h.c" \
    || fail "isr80h.c: command 18 not registered"
grep -q 'isr80h_command18_get_window_event' "$ROOT/src/isr80h/window.h" \
    || fail "window.h: command 18 decl missing"
grep -q 'isr80h_command18_get_window_event' "$ROOT/src/isr80h/window.c" \
    || fail "window.c: command 18 body missing"
grep -q 'process_pop_window_event(task_current()->process' "$ROOT/src/isr80h/window.c" \
    || fail "window.c: command 18 does not pop event"
grep -q 'window_event_to_userland(' "$ROOT/src/isr80h/window.c" \
    || fail "window.c: command 18 does not translate to userland struct"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L163 isr80h get window event"
