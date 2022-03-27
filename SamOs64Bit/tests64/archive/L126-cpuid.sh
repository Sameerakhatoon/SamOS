#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

[ -f "$ROOT/src/io/cpuid.h" ] || fail "cpuid.h missing"
[ -f "$ROOT/src/io/cpuid.c" ] || fail "cpuid.c missing"
grep -q 'void cpuid(uint32_t leaf' "$ROOT/src/io/cpuid.h" || fail "cpuid.h: prototype missing"
grep -q 'asm volatile("cpuid"' "$ROOT/src/io/cpuid.c" || fail "cpuid.c: asm missing"

grep -q 'build/io/cpuid.o' "$ROOT/Makefile" || fail "Makefile: cpuid.o not in FILES"
grep -q '\./build/io/cpuid.o:' "$ROOT/Makefile" || fail "Makefile: cpuid.o recipe missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L126 cpuid"
