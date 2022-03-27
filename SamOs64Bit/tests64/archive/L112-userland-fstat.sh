#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q 'SYSTEM_COMMAND14_FSTAT' "$ROOT/src/isr80h/isr80h.h" || fail "enum missing"
grep -q 'isr80h_command14_fstat' "$ROOT/src/isr80h/file.c" || fail "handler missing"
grep -q 'isr80h_command14_fstat' "$ROOT/src/isr80h/isr80h.c" || fail "register missing"
grep -q '#include "fs/file.h"' "$ROOT/src/isr80h/file.c" || fail "isr80h/file.c: fs/file.h include missing"

grep -q 'process_fstat' "$ROOT/src/task/process.h" || fail "process.h: fstat decl missing"
grep -q 'process_fstat' "$ROOT/src/task/process.c" || fail "process.c: fstat body missing"
grep -q 'process_virtual_address_to_physical' "$ROOT/src/task/process.c" \
    || fail "process.c: v2p helper missing"

grep -q 'samos_fstat' "$ROOT/programs/stdlib/src/samos.asm" || fail "samos.asm: trampoline missing"
grep -q 'samos_fstat' "$ROOT/programs/stdlib/src/samos.h" || fail "samos.h: decl missing"
grep -q 'struct file_stat' "$ROOT/programs/stdlib/src/file.h" || fail "stdlib/file.h: struct missing"
grep -q 'int fstat(' "$ROOT/programs/stdlib/src/file.h" || fail "stdlib/file.h: fstat decl missing"
grep -q 'samos_fstat(fd, file_stat_out)' "$ROOT/programs/stdlib/src/file.c" \
    || fail "stdlib/file.c: fstat body missing"

grep -q 'fstat(fd, &file_stat)' "$ROOT/programs/blank/blank.c" \
    || fail "blank.c: fstat call missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L112 userland fstat"
