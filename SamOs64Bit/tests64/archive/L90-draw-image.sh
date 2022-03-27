#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q 'graphics_draw_image' "$ROOT/src/graphics/graphics.h" \
    || fail "graphics.h: draw_image prototype missing"
grep -q 'void graphics_draw_image(' "$ROOT/src/graphics/graphics.c" \
    || fail "graphics.c: draw_image definition missing"
grep -q '#include "graphics/image/image.h"' "$ROOT/src/graphics/graphics.h" \
    || fail "graphics.h: image.h include missing"

grep -q 'graphics_image_load("@:/bkground.bmp")' "$ROOT/src/kernel.c" \
    || fail "kernel.c: bkground.bmp load missing"
grep -q 'graphics_draw_image(NULL' "$ROOT/src/kernel.c" \
    || fail "kernel.c: draw_image call missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L90 draw_image"
