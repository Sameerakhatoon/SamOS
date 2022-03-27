#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q 'SYSTEM_COMMAND13_FSEEK' "$ROOT/src/isr80h/isr80h.h" \
    || fail "isr80h.h: enum missing"
grep -q 'isr80h_command13_fseek' "$ROOT/src/isr80h/file.c" \
    || fail "isr80h/file.c: handler missing"
grep -q 'isr80h_command13_fseek' "$ROOT/src/isr80h/isr80h.c" \
    || fail "isr80h.c: registration missing"

grep -q 'process_fseek' "$ROOT/src/task/process.h" \
    || fail "process.h: prototype missing"
grep -q 'process_fseek' "$ROOT/src/task/process.c" \
    || fail "process.c: body missing"
grep -q '#include "fs/file.h"' "$ROOT/src/task/process.h" \
    || fail "process.h: fs/file.h include missing"

grep -q 'samos_fseek' "$ROOT/programs/stdlib/src/samos.asm" \
    || fail "samos.asm: trampoline missing"
grep -q 'samos_fseek' "$ROOT/programs/stdlib/src/samos.h" \
    || fail "samos.h: trampoline decl missing"
grep -q 'int  fseek(' "$ROOT/programs/stdlib/src/file.h" \
    || fail "stdlib/file.h: fseek decl missing"
grep -q 'samos_fseek(fd, offset, whence)' "$ROOT/programs/stdlib/src/file.c" \
    || fail "stdlib/file.c: fseek body missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L111 userland fseek"
