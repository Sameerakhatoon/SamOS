#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

[ -f "$ROOT/src/graphics/terminal.h" ] || fail "terminal.h missing"
[ -f "$ROOT/src/graphics/terminal.c" ] || fail "terminal.c missing"

grep -q 'struct terminal' "$ROOT/src/graphics/terminal.h" || fail "struct terminal missing"
grep -q 'TERMINAL_FLAG_BACKSPACE_ALLOWED' "$ROOT/src/graphics/terminal.h" \
    || fail "backspace flag missing"
grep -q 'terminal_background' "$ROOT/src/graphics/terminal.h" \
    || fail "terminal_background field missing"

grep -q 'terminal_create' "$ROOT/src/graphics/terminal.c" \
    || fail "terminal_create missing"
grep -q 'terminal_system_setup' "$ROOT/src/graphics/terminal.c" \
    || fail "terminal_system_setup missing"
grep -q 'terminal_background_save' "$ROOT/src/graphics/terminal.c" \
    || fail "terminal_background_save reference missing"

# terminal.o must NOT yet be in FILES (L97 adds it).
if grep -q 'build/graphics/terminal.o' "$ROOT/Makefile"; then
    fail "Makefile: terminal.o should not be in FILES at L96"
fi

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L96 terminal part 1"
