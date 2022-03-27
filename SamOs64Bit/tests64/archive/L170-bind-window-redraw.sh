#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q 'SYSTEM_COMMAND21_WINDOW_REDRAW.*isr80h_command21_window_redraw' \
    "$ROOT/src/isr80h/isr80h.c" \
    || fail "isr80h.c: command 21 not registered"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L170 bind window redraw"
