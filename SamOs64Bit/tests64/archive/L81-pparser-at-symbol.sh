#!/usr/bin/env bash
# L81 - "@" drive character resolves to disk_primary_fs_disk().
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)

fail(){ echo "FAIL: $*" >&2; exit 1; }

# pparser accepts '@' in valid_format.
grep -q "filename\[0\] == '@'" "$ROOT/src/fs/pparser.c" \
    || fail "pparser.c: valid_format does not accept '@'"

# pparser resolves '@' via disk_primary_fs_disk.
grep -q 'disk_primary_fs_disk' "$ROOT/src/fs/pparser.c" \
    || fail "pparser.c: '@' must resolve through disk_primary_fs_disk"
grep -q '#include "disk/disk.h"' "$ROOT/src/fs/pparser.c" \
    || fail "pparser.c: missing disk.h include"

# kernel.c uses the new syntax.
grep -q '"@:/SIMPLE.BIN"\|"@:/shell.elf"\|"@:/SHELL.ELF"' "$ROOT/src/kernel.c" \
    || fail "kernel.c: process_load_switch path must use '@:'"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"

echo "PASS: L81 pparser @-drive"
