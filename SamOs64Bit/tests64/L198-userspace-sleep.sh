#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

[ -f "$ROOT/src/isr80h/time.h" ] || fail "time.h missing"
[ -f "$ROOT/src/isr80h/time.c" ] || fail "time.c missing"

grep -q 'SYSTEM_COMMAND25_UDELAY' "$ROOT/src/isr80h/isr80h.h" \
    || fail "isr80h.h: command 25 missing"
grep -q 'SYSTEM_COMMAND25_UDELAY' "$ROOT/src/isr80h/isr80h.c" \
    || fail "isr80h.c: command 25 not registered"
grep -q 'isr80h_command25_udelay' "$ROOT/src/isr80h/time.c" \
    || fail "time.c: command 25 body missing"

# Multiplication typo fixed.
grep -q 'tsc_microseconds() + microseconds' "$ROOT/src/task/task.c" \
    || fail "task.c: L198 part 2 fix not applied"

grep -q 'build/isr80h/time.o' "$ROOT/Makefile" || fail "Makefile: time.o missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L198 userspace sleep"
