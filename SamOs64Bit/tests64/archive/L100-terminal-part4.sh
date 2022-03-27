#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# VGA terminal path retired.
if grep -q 'terminal_initialize' "$ROOT/src/kernel.c"; then
    if grep -qE '^[[:space:]]*terminal_initialize\(' "$ROOT/src/kernel.c"; then
        fail "kernel.c: terminal_initialize call should be removed"
    fi
fi
if grep -qE 'void terminal_putchar\(' "$ROOT/src/kernel.c"; then
    fail "kernel.c: legacy terminal_putchar should be removed"
fi
if grep -qE 'void vga_terminal_backspace\(' "$ROOT/src/kernel.c"; then
    fail "kernel.c: vga_terminal_backspace should be removed"
fi
# The legacy VGA address should no longer appear in code (only
# comments may mention it).
if grep -E '^[^/]*0xB8000' "$ROOT/src/kernel.c" | grep -v '^[[:space:]]*//' > /dev/null; then
    fail "kernel.c: VGA video_mem at 0xB8000 still referenced in code"
fi

# system_terminal handle and forwarder.
grep -q 'struct terminal\* system_terminal' "$ROOT/src/kernel.c" \
    || fail "kernel.c: system_terminal handle missing"
grep -q 'terminal_write(system_terminal' "$ROOT/src/kernel.c" \
    || fail "kernel.c: terminal_writechar must forward to terminal_write"

# Boot wiring.
grep -q 'terminal_system_setup()' "$ROOT/src/kernel.c" \
    || fail "kernel.c: terminal_system_setup missing"
grep -q 'system_terminal = terminal_create' "$ROOT/src/kernel.c" \
    || fail "kernel.c: terminal_create call missing"
grep -q 'TERMINAL_FLAG_BACKSPACE_ALLOWED' "$ROOT/src/kernel.c" \
    || fail "kernel.c: backspace flag missing in terminal_create"

# Header.
grep -q '#include "graphics/terminal.h"' "$ROOT/src/kernel.c" \
    || fail "kernel.c: terminal.h include missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L100 terminal part 4"
