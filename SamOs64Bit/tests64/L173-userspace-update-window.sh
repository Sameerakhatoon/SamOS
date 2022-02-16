#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q 'SYSTEM_COMMAND24_UPDATE_WINDOW' "$ROOT/src/isr80h/isr80h.h" || fail "isr80h.h: cmd24 missing"
grep -q 'SYSTEM_COMMAND24_UPDATE_WINDOW' "$ROOT/src/isr80h/isr80h.c" || fail "isr80h.c: cmd24 not registered"
grep -q 'ISR80H_WINDOW_UPDATE_TITLE' "$ROOT/src/isr80h/window.h" || fail "window.h: subcommand enum missing"
grep -q 'isr80h_command24_update_window' "$ROOT/src/isr80h/window.h" || fail "window.h: cmd24 decl missing"
grep -q 'isr80h_command24_update_window' "$ROOT/src/isr80h/window.c" || fail "window.c: cmd24 body missing"

# Preserve upstream isr90h typo verbatim.
grep -q 'isr90h_command24_update_window_title' "$ROOT/src/isr80h/window.c" \
    || fail "window.c: upstream isr90h_command24_update_window_title typo not preserved"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L173 userspace update window"
