#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

[ -f "$ROOT/src/io/tsc.h" ] || fail "tsc.h missing"
[ -f "$ROOT/src/io/tsc.c" ] || fail "tsc.c missing"

for sym in TIME_TSC TIME_MICROSECONDS TIME_MILISECONDS TIME_SECONDS \
           tsc_frequency udelay tsc_seconds tsc_miliseconds \
           read_tsc tsc_microseconds; do
    grep -q "$sym" "$ROOT/src/io/tsc.h" || fail "tsc.h: $sym missing"
done

grep -q 'cpuid(0x15, 0' "$ROOT/src/io/tsc.c" || fail "tsc.c: leaf 0x15 missing"
grep -q 'cpuid(0x16, 0' "$ROOT/src/io/tsc.c" || fail "tsc.c: leaf 0x16 missing"
grep -q '"lfence; rdtsc"' "$ROOT/src/io/tsc.c" || fail "tsc.c: lfence+rdtsc missing"
grep -q '"pause"' "$ROOT/src/io/tsc.c" || fail "tsc.c: pause spin missing"

# vecotr_at typo fix from L123 workaround. Comments may still
# mention the historic typo; the live code must not.
if grep -E '^[^/]*vecotr_at\(' "$ROOT/src/graphics/window.c" \
    | grep -v '^[[:space:]]*//' > /dev/null; then
    fail "window.c: L128 should have replaced vecotr_at() with vector_at()"
fi

# tsc.o should NOT yet be in build.
if grep -q 'build/io/tsc.o' "$ROOT/Makefile"; then
    fail "Makefile: tsc.o should not yet be in FILES at L128"
fi

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L128 tsc delay"
