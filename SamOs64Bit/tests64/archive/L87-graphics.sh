#!/usr/bin/env bash
# L87 - graphics module foundations.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)

fail(){ echo "FAIL: $*" >&2; exit 1; }

# Source files exist.
[ -f "$ROOT/src/graphics/graphics.h" ] || fail "graphics.h missing"
[ -f "$ROOT/src/graphics/graphics.c" ] || fail "graphics.c missing"

# Public API.
for sym in graphics_setup graphics_screen_info graphics_draw_pixel \
           graphics_redraw graphics_redraw_all; do
    grep -q "$sym" "$ROOT/src/graphics/graphics.h" \
        || fail "graphics.h: $sym not declared"
    grep -q "$sym" "$ROOT/src/graphics/graphics.c" \
        || fail "graphics.c: $sym not implemented"
done

# struct framebuffer_pixel BGR(R)A order.
grep -q 'uint8_t\s\+blue' "$ROOT/src/graphics/graphics.h" \
    || fail "graphics.h: framebuffer_pixel.blue missing"
grep -q 'uint8_t\s\+green' "$ROOT/src/graphics/graphics.h" \
    || fail "graphics.h: framebuffer_pixel.green missing"
grep -q 'uint8_t\s\+red' "$ROOT/src/graphics/graphics.h" \
    || fail "graphics.h: framebuffer_pixel.red missing"

# default_graphics_info exported from kernel.asm.
grep -q 'global default_graphics_info' "$ROOT/src/kernel.asm" \
    || fail "kernel.asm: default_graphics_info not exported"
grep -q 'default_graphics_info:' "$ROOT/src/kernel.asm" \
    || fail "kernel.asm: default_graphics_info label missing"
grep -q 'mov \[default_graphics_info + 0\],  rdi' "$ROOT/src/kernel.asm" \
    || fail "kernel.asm: framebuffer ptr not captured from rdi"
grep -q 'mov \[default_graphics_info + 8\],  edx' "$ROOT/src/kernel.asm" \
    || fail "kernel.asm: horizontal resolution not captured from edx"

# kernel.c imports + calls + paints red.
grep -q '#include "graphics/graphics.h"' "$ROOT/src/kernel.c" \
    || fail "kernel.c: graphics include missing"
grep -q 'extern struct graphics_info default_graphics_info' "$ROOT/src/kernel.c" \
    || fail "kernel.c: default_graphics_info extern missing"
grep -q 'graphics_setup(&default_graphics_info)' "$ROOT/src/kernel.c" \
    || fail "kernel.c: graphics_setup call missing"
grep -q 'graphics_redraw_all()' "$ROOT/src/kernel.c" \
    || fail "kernel.c: graphics_redraw_all call missing"
grep -q 'red.red = 0xff' "$ROOT/src/kernel.c" \
    || fail "kernel.c: red sanity paint missing"

# Build hooks.
grep -q 'build/graphics/graphics.o' "$ROOT/Makefile" \
    || fail "Makefile: graphics.o not in FILES"
grep -q 'build/graphics' "$ROOT/build.sh" \
    || fail "build.sh: build/graphics dir not created"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"

echo "PASS: L87 graphics"
