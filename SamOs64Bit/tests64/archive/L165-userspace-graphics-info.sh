#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

[ -f "$ROOT/src/isr80h/graphics.h" ] || fail "graphics.h missing"
[ -f "$ROOT/src/isr80h/graphics.c" ] || fail "graphics.c missing"

grep -q 'struct userland_graphics' "$ROOT/src/isr80h/graphics.h" \
    || fail "graphics.h: userland_graphics missing"
grep -q 'isr80h_graphics_make_userland_metadata' "$ROOT/src/isr80h/graphics.h" \
    || fail "graphics.h: builder decl missing"
grep -q 'isr80h_graphics_make_userland_metadata' "$ROOT/src/isr80h/graphics.c" \
    || fail "graphics.c: builder body missing"

grep -q 'SYSTEM_COMMAND19_WINDOW_GRAPHICS_GET' "$ROOT/src/isr80h/isr80h.h" \
    || fail "isr80h.h: command 19 missing"
grep -q 'SYSTEM_COMMAND19_WINDOW_GRAPHICS_GET' "$ROOT/src/isr80h/isr80h.c" \
    || fail "isr80h.c: command 19 not registered"
grep -q 'isr80h_command19_window_graphics_get' "$ROOT/src/isr80h/window.c" \
    || fail "window.c: command 19 body missing"

grep -q 'build/isr80h/graphics.o' "$ROOT/Makefile" \
    || fail "Makefile: graphics.o missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L165 userspace graphics info"
