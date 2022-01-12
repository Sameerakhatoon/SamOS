#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

[ -f "$ROOT/src/io/tsc.asm" ] || fail "tsc.asm missing"
grep -q '^tsc_microseconds:' "$ROOT/src/io/tsc.asm" || fail "tsc.asm: label missing"
grep -q 'global tsc_microseconds' "$ROOT/src/io/tsc.asm" || fail "tsc.asm: global missing"
grep -q 'extern read_tsc' "$ROOT/src/io/tsc.asm" || fail "tsc.asm: extern read_tsc missing"
grep -q 'extern tsc_frequency' "$ROOT/src/io/tsc.asm" || fail "tsc.asm: extern tsc_frequency missing"
grep -q 'mov  rcx, 1000000' "$ROOT/src/io/tsc.asm" || fail "tsc.asm: 1e6 multiplier missing"

# tsc.c fix for tsc_miliseconds parens.
grep -q 'tsc_miliseconds()' "$ROOT/src/io/tsc.c" || fail "tsc.c: parens still missing"

# tsc.o + tsc.asm.o in build.
grep -q 'build/io/tsc.o' "$ROOT/Makefile" || fail "Makefile: tsc.o not in FILES"
grep -q 'build/io/tsc.asm.o' "$ROOT/Makefile" || fail "Makefile: tsc.asm.o not in FILES"

# kernel.c smoke loop.
grep -q '"Another second' "$ROOT/src/kernel.c" || fail "kernel.c: udelay smoke print missing"
grep -q 'udelay(1000000)' "$ROOT/src/kernel.c" || fail "kernel.c: udelay call missing"
grep -q '#include "io/tsc.h"' "$ROOT/src/kernel.c" || fail "kernel.c: tsc.h include missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L129 tsc link + udelay smoke"
