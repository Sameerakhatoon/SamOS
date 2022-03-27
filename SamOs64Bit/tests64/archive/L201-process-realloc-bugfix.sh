#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q 'old_virt_ptr == NULL' "$ROOT/src/task/process.c" \
    || fail "process.c: NULL early-out missing"
grep -q 'return process_malloc(process, new_size)' "$ROOT/src/task/process.c" \
    || fail "process.c: realloc(NULL, n) fallthrough missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L201 process realloc bugfix"
