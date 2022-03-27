#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q 'SYSTEM_COMMAND12_FREAD' "$ROOT/src/isr80h/isr80h.h" \
    || fail "isr80h.h: enum entry missing"
grep -q 'isr80h_command12_fread' "$ROOT/src/isr80h/file.c" \
    || fail "isr80h/file.c: command 12 missing"
grep -q 'isr80h_command12_fread' "$ROOT/src/isr80h/isr80h.c" \
    || fail "isr80h.c: register missing"

grep -q 'process_fread' "$ROOT/src/task/process.h" \
    || fail "process.h: prototype missing"
grep -q 'process_fread' "$ROOT/src/task/process.c" \
    || fail "process.c: body missing"
grep -q 'process_validate_memory_or_terminate' "$ROOT/src/task/process.c" \
    || fail "process.c: validate stub missing"

grep -q 'samos_read' "$ROOT/programs/stdlib/src/samos.asm" \
    || fail "samos.asm: trampoline missing"
grep -q 'samos_read' "$ROOT/programs/stdlib/src/samos.h" \
    || fail "samos.h: trampoline decl missing"
grep -q 'fread(void\* buffer' "$ROOT/programs/stdlib/src/file.h" \
    || fail "stdlib/file.h: fread decl missing"
grep -q 'samos_read(buffer, size, count, fd)' "$ROOT/programs/stdlib/src/file.c" \
    || fail "stdlib/file.c: fread body missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L107 userland fread"
