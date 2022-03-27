#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q 'SYSTEM_COMMAND11_FCLOSE' "$ROOT/src/isr80h/isr80h.h" \
    || fail "isr80h.h: FCLOSE missing"
grep -q 'SYSTEM_COMMAND11_FCLOSE' "$ROOT/src/isr80h/isr80h.c" \
    || fail "isr80h.c: FCLOSE not registered"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L202 userland fclose (kernel-side invariant)"
