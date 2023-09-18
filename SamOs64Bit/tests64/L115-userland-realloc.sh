#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q 'SYSTEM_COMMAND15_REALLOC' "$ROOT/src/isr80h/isr80h.h" || fail "enum missing"
grep -q 'isr80h_command15_realloc' "$ROOT/src/isr80h/heap.c" || fail "handler missing"
grep -q 'isr80h_command15_realloc' "$ROOT/src/isr80h/isr80h.c" || fail "register missing"
grep -q 'ENOTFOUND' "$ROOT/src/status.h" || fail "ENOTFOUND missing"

grep -q 'process_realloc' "$ROOT/src/task/process.h" || fail "process.h: decl missing"
grep -q 'process_realloc' "$ROOT/src/task/process.c" || fail "process.c: body missing"
grep -q 'process_allocation_exists' "$ROOT/src/task/process.c" || fail "process_allocation_exists missing"

grep -q 'samos_realloc' "$ROOT/programs/stdlib/src/samos.asm" || fail "samos.asm: trampoline missing"
grep -q 'samos_realloc' "$ROOT/programs/stdlib/src/samos.h" || fail "samos.h: decl missing"
grep -q 'realloc(void\* ptr' "$ROOT/programs/stdlib/src/stdlib.h" || fail "stdlib.h: realloc decl missing"
grep -q 'calloc(size_t' "$ROOT/programs/stdlib/src/stdlib.h" || fail "stdlib.h: calloc decl missing"
grep -q 'samos_realloc(ptr, new_size)' "$ROOT/programs/stdlib/src/stdlib.c" \
    || fail "stdlib.c: realloc body missing"
grep -q 'memset(ptr, 0, b_size)' "$ROOT/programs/stdlib/src/stdlib.c" \
    || fail "stdlib.c: calloc memset missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L115 userland realloc"
