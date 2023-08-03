#!/usr/bin/env bash
# L88 part 1 - image format registry and load-from-path skeleton.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)

fail(){ echo "FAIL: $*" >&2; exit 1; }

[ -f "$ROOT/src/graphics/image/image.h" ] || fail "image.h missing"
[ -f "$ROOT/src/graphics/image/image.c" ] || fail "image.c missing"

for sym in graphics_image_formats_init graphics_image_formats_load \
           graphics_image_formats_unload graphics_image_format_register \
           graphics_image_format_get graphics_image_load \
           graphics_image_load_from_memory graphics_image_free \
           graphics_image_get_pixel; do
    grep -q "$sym" "$ROOT/src/graphics/image/image.h" \
        || fail "image.h: $sym not declared"
    grep -q "$sym" "$ROOT/src/graphics/image/image.c" \
        || fail "image.c: $sym not implemented"
done

grep -q 'union image_pixel_data' "$ROOT/src/graphics/image/image.h" \
    || fail "image.h: image_pixel_data union missing"
grep -q 'IMAGE_FORMAT_MAX_MIME_TYPE' "$ROOT/src/graphics/image/image.h" \
    || fail "image.h: max MIME type macro missing"

# Build hooks.
grep -q 'build/graphics/image/image.o' "$ROOT/Makefile" \
    || fail "Makefile: image.o not in FILES"
grep -q 'build/graphics/image' "$ROOT/build.sh" \
    || fail "build.sh: build/graphics/image dir missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"

echo "PASS: L88 part 1 image abstraction"
