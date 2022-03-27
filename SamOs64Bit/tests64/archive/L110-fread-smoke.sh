#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# Trampoline renamed to samos_fread.
grep -q 'samos_fread' "$ROOT/programs/stdlib/src/samos.asm" \
    || fail "samos.asm: samos_fread missing"
grep -q 'samos_fread' "$ROOT/programs/stdlib/src/samos.h" \
    || fail "samos.h: samos_fread decl missing"
grep -q 'samos_fread' "$ROOT/programs/stdlib/src/file.c" \
    || fail "file.c: must call samos_fread (rename)"
if grep -q 'samos_read' "$ROOT/programs/stdlib/src/file.c"; then
    fail "file.c: stale samos_read reference"
fi

# blank.c smoke calls fread.
grep -q 'fread(buf, 1, sizeof(buf), fd)' "$ROOT/programs/blank/blank.c" \
    || fail "blank.c: fread smoke missing"
grep -q 'char buf\[512\]' "$ROOT/programs/blank/blank.c" \
    || fail "blank.c: buf decl missing"

# Kernel boots blank.elf, not SIMPLE.BIN.
grep -q '"@:/blank.elf"' "$ROOT/src/kernel.c" \
    || fail "kernel.c: must load @:/blank.elf"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L110 fread smoke"
