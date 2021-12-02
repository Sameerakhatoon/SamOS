#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

[ -f "$ROOT/src/graphics/image/bmp.h" ] || fail "bmp.h missing"
[ -f "$ROOT/src/graphics/image/bmp.c" ] || fail "bmp.c missing"

grep -q 'struct bmp_header' "$ROOT/src/graphics/image/bmp.h" \
    || fail "bmp.h: header struct missing"
grep -q 'struct bmp_image_header' "$ROOT/src/graphics/image/bmp.h" \
    || fail "bmp.h: info struct missing"
grep -q 'BMP_SIGNATURE' "$ROOT/src/graphics/image/bmp.h" \
    || fail "bmp.h: signature define missing"

grep -q 'bmp_img_load' "$ROOT/src/graphics/image/bmp.c" \
    || fail "bmp.c: bmp_img_load missing"
grep -q 'graphics_image_format_bmp_setup' "$ROOT/src/graphics/image/bmp.c" \
    || fail "bmp.c: setup factory missing"
grep -q 'image/bmp' "$ROOT/src/graphics/image/bmp.c" \
    || fail "bmp.c: MIME string missing"

grep -q 'graphics_image_format_bmp_setup' "$ROOT/src/graphics/image/image.c" \
    || fail "image.c: BMP not registered in formats_load"
grep -q 'graphics_image_formats_init' "$ROOT/src/graphics/graphics.c" \
    || fail "graphics.c: formats_init not called from graphics_setup"

grep -q 'build/graphics/image/bmp.o' "$ROOT/Makefile" \
    || fail "Makefile: bmp.o not in FILES"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"

echo "PASS: L88 part 2 BMP loader"
