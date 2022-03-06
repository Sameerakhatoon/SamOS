#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q '^.PHONY: all clean user_programs user_programs_clean' "$ROOT/Makefile" \
    || fail "Makefile: .PHONY decl missing"

# All user-program targets use $(MAKE) -C
grep -q '\$(MAKE) -C ./programs/stdlib' "$ROOT/Makefile" \
    || fail "Makefile: stdlib not built via -C"
grep -q '\$(MAKE) -C ./programs/blank' "$ROOT/Makefile" \
    || fail "Makefile: blank not built via -C"

grep -q '^user_programs_clean:' "$ROOT/Makefile" \
    || fail "Makefile: user_programs_clean target missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L200 makefile improvements"
