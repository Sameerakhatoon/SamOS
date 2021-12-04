#!/usr/bin/env bash
# L92 - font module source-only landing (not yet in build).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

[ -f "$ROOT/src/graphics/font.h" ] || fail "font.h missing"
[ -f "$ROOT/src/graphics/font.c" ] || fail "font.c missing"

grep -q 'struct font' "$ROOT/src/graphics/font.h" \
    || fail "font.h: struct font missing"
grep -q 'bits_height_per_chracter' "$ROOT/src/graphics/font.h" \
    || fail "font.h: upstream typo bits_height_per_chracter not preserved"
grep -q 'FONT_IMAGE_CHARACTER_WIDTH_PIXEL_SIZE' "$ROOT/src/graphics/font.h" \
    || fail "font.h: pixel width macro missing"

grep -q 'font_load_from_image' "$ROOT/src/graphics/font.c" \
    || fail "font.c: font_load_from_image missing"
grep -q 'font_create' "$ROOT/src/graphics/font.c" \
    || fail "font.c: font_create missing"

# font.o must NOT be in FILES yet - upstream L92 leaves it out.
if grep -q 'build/graphics/font.o' "$ROOT/Makefile"; then
    fail "Makefile: font.o should not yet be in FILES at L92"
fi

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L92 font foundation"
