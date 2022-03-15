#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# kernel.c default user program switched to shell.elf.
grep -q 'process_load_switch("@:/shell.elf"' "$ROOT/src/kernel.c" \
    || fail "kernel.c: default user program not switched to shell.elf"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L208 deps and calculator (kernel-side parity)"
