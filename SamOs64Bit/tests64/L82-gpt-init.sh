#!/usr/bin/env bash
# L82 - GPT module gets compiled and gpt_init called from kernel_main.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)

fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q 'build/disk/gpt.o' "$ROOT/Makefile" \
    || fail "Makefile: gpt.o not in FILES"
grep -q '\./build/disk/gpt.o: \./src/disk/gpt.c' "$ROOT/Makefile" \
    || fail "Makefile: gpt.o compile rule missing"

grep -q '#include "disk/gpt.h"' "$ROOT/src/kernel.c" \
    || fail "kernel.c: missing disk/gpt.h include"
grep -q 'gpt_init();' "$ROOT/src/kernel.c" \
    || fail "kernel.c: gpt_init() call missing"

# gpt_init must follow disk_search_and_init.
awk '/disk_search_and_init/{d=NR} /gpt_init\(\)/{if(!d || NR<d) exit 1; exit 0}' \
    "$ROOT/src/kernel.c" || fail "kernel.c: gpt_init must come after disk_search_and_init"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"

echo "PASS: L82 gpt_init"
