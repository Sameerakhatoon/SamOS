#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q 'SYSTEM_COMMAND11_FCLOSE' "$ROOT/src/isr80h/isr80h.h" \
    || fail "isr80h.h: enum entry missing"
grep -q 'isr80h_command11_fclose' "$ROOT/src/isr80h/file.c" \
    || fail "isr80h/file.c: command 11 missing"
grep -q 'isr80h_command11_fclose' "$ROOT/src/isr80h/isr80h.c" \
    || fail "isr80h.c: register missing"

grep -q 'process_fclose' "$ROOT/src/task/process.h" \
    || fail "process.h: prototype missing"
grep -q 'process_fclose' "$ROOT/src/task/process.c" \
    || fail "process.c: body missing"

grep -q 'samos_fclose' "$ROOT/programs/stdlib/src/samos.asm" \
    || fail "samos.asm: trampoline missing"
grep -q 'samos_fclose' "$ROOT/programs/stdlib/src/samos.h" \
    || fail "samos.h: trampoline decl missing"
grep -q 'void fclose(int fd)' "$ROOT/programs/stdlib/src/file.h" \
    || fail "stdlib/file.h: fclose decl missing"
grep -q 'samos_fclose((size_t)fd)' "$ROOT/programs/stdlib/src/file.c" \
    || fail "stdlib/file.c: fclose body missing"

grep -q 'fclose(fd);' "$ROOT/programs/blank/blank.c" \
    || fail "blank.c: fclose call missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L106 userland fclose"
