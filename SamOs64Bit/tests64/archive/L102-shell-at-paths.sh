#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# Both isr80h process load paths use the L81 @-prefix.
if grep -c '"0:/"' "$ROOT/src/isr80h/process.c" | grep -qv '^0$'; then
    fail "isr80h/process.c: legacy 0:/ prefix still present"
fi
n=$(grep -c '"@:/"' "$ROOT/src/isr80h/process.c")
[ "$n" -ge 2 ] || fail "isr80h/process.c: expected 2 @:/ uses, found $n"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L102 shell @ paths"
