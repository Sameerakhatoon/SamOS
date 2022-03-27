#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PROJ=$(cd "$ROOT/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q 'Skylake-Server' "$PROJ/run.sh" || fail "run.sh: -cpu Skylake-Server not set"
grep -q 'tsc-frequency=2800000000' "$PROJ/run.sh" || fail "run.sh: tsc-frequency missing"
if grep -q -- '-cpu qemu64' "$PROJ/run.sh"; then
    fail "run.sh: stale qemu64 cpu line"
fi

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L130 qemu cpu skylake-server"
