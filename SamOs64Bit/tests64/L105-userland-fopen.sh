#!/usr/bin/env bash
# L105 - userland fopen syscall (command 10).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# Kernel-side syscall handler.
[ -f "$ROOT/src/isr80h/file.h" ] || fail "isr80h/file.h missing"
[ -f "$ROOT/src/isr80h/file.c" ] || fail "isr80h/file.c missing"
grep -q 'isr80h_command10_fopen' "$ROOT/src/isr80h/file.c" \
    || fail "isr80h/file.c: command 10 handler missing"
grep -q 'SYSTEM_COMMAND10_FOPEN' "$ROOT/src/isr80h/isr80h.h" \
    || fail "isr80h.h: enum entry missing"
grep -q 'isr80h_command10_fopen' "$ROOT/src/isr80h/isr80h.c" \
    || fail "isr80h.c: command 10 registration missing"

# Process bookkeeping.
grep -q 'struct process_file_handle' "$ROOT/src/task/process.h" \
    || fail "process.h: process_file_handle struct missing"
grep -q 'file_handles' "$ROOT/src/task/process.h" \
    || fail "process.h: file_handles field missing"
grep -q 'process_fopen' "$ROOT/src/task/process.h" \
    || fail "process.h: process_fopen prototype missing"
grep -q 'process_fopen' "$ROOT/src/task/process.c" \
    || fail "process.c: process_fopen body missing"
grep -q 'process_close_file_handles' "$ROOT/src/task/process.c" \
    || fail "process.c: close_file_handles missing"

# Makefile.
grep -q 'build/isr80h/file.o' "$ROOT/Makefile" \
    || fail "Makefile: isr80h/file.o not in FILES"

# Userland.
PROG="$ROOT/programs/stdlib/src"
[ -f "$PROG/file.h" ] || fail "stdlib/file.h missing"
[ -f "$PROG/file.c" ] || fail "stdlib/file.c missing"
grep -q 'samos_fopen' "$PROG/samos.h" || fail "samos.h: samos_fopen missing"
grep -q 'samos_fopen' "$PROG/samos.asm" || fail "samos.asm: samos_fopen missing"
grep -q 'int fopen(' "$PROG/file.h" || fail "stdlib/file.h: fopen() missing"
grep -q 'samos_fopen(filename, mode)' "$PROG/file.c" \
    || fail "stdlib/file.c: fopen does not forward to samos_fopen"

# blank.c smoke calls fopen.
grep -q 'fopen("@:/blank.elf"' "$ROOT/programs/blank/blank.c" \
    || fail "blank.c: fopen smoke missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L105 userland fopen"
