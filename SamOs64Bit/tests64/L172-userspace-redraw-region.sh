#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q 'SYSTEM_COMMAND23_WINDOW_REDRAW_REGION' "$ROOT/src/isr80h/isr80h.h" || fail "isr80h.h: cmd23 missing"
grep -q 'SYSTEM_COMMAND23_WINDOW_REDRAW_REGION' "$ROOT/src/isr80h/isr80h.c" || fail "isr80h.c: cmd23 not registered"
grep -q 'isr80h_command23_window_redraw_region' "$ROOT/src/isr80h/window.h" || fail "window.h: cmd23 decl missing"
grep -q 'isr80h_command23_window_redraw_region' "$ROOT/src/isr80h/window.c" || fail "window.c: cmd23 body missing"
grep -q 'window_redraw_body_region(kern_window' "$ROOT/src/isr80h/window.c" || fail "window.c: cmd23 does not call redraw_body_region"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L172 userspace redraw region"
