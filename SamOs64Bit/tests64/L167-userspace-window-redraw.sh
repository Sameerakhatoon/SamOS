#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q 'SYSTEM_COMMAND21_WINDOW_REDRAW' "$ROOT/src/isr80h/isr80h.h" \
    || fail "isr80h.h: command 21 missing"
grep -q 'isr80h_command21_window_redraw' "$ROOT/src/isr80h/window.h" \
    || fail "window.h: command 21 decl missing"
grep -q 'isr80h_command21_window_redraw' "$ROOT/src/isr80h/window.c" \
    || fail "window.c: command 21 body missing"
grep -q 'window_redraw(kern_window)' "$ROOT/src/isr80h/window.c" \
    || fail "window.c: command 21 does not call window_redraw"
# L170 binds it; not registered yet at L167.
grep -q 'SYSTEM_COMMAND21_WINDOW_REDRAW' "$ROOT/src/isr80h/isr80h.c" \
    && fail "isr80h.c: command 21 registered prematurely (L170 should do this)" || true

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L167 userspace window redraw"
