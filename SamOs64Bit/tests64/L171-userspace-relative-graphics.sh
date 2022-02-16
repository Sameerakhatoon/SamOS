#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q 'SYSTEM_COMMAND22_GRAPHICS_CREATE' "$ROOT/src/isr80h/isr80h.h" \
    || fail "isr80h.h: command 22 missing"
grep -q 'SYSTEM_COMMAND22_GRAPHICS_CREATE' "$ROOT/src/isr80h/isr80h.c" \
    || fail "isr80h.c: command 22 not registered"
grep -q 'isr80h_command22_graphics_create' "$ROOT/src/isr80h/graphics.h" \
    || fail "graphics.h: command 22 decl missing"
grep -q 'isr80h_command22_graphics_create' "$ROOT/src/isr80h/graphics.c" \
    || fail "graphics.c: command 22 body missing"
grep -q 'graphics_info_create_relative(kernel_land_graphics_ptr' "$ROOT/src/isr80h/graphics.c" \
    || fail "graphics.c: command 22 does not call create_relative"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L171 userspace relative graphics"
