#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

for sym in graphics_bounds_check graphics_pixel_get graphics_set_z_index \
           graphics_reorder graphics_paste_pixels_to_pixels \
           graphics_info_create_relative graphics_info_free \
           graphics_info_children_free; do
    grep -q "$sym" "$ROOT/src/graphics/graphics.c" || fail "graphics.c: $sym missing"
done

for sym in graphics_set_z_index graphics_info_create_relative \
           graphics_paste_pixels_to_pixels graphics_pixel_get \
           graphics_info_free; do
    grep -q "$sym" "$ROOT/src/graphics/graphics.h" || fail "graphics.h: $sym missing"
done

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L116 graphics essentials"
