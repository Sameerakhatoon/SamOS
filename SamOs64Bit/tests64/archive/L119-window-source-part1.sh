#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

[ -f "$ROOT/src/graphics/window.c" ] || fail "window.c missing"

grep -q 'WINDOW_BORDER_PIXEL_SIZE' "$ROOT/src/config.h" || fail "config.h: border size missing"
grep -q 'WINDOW_TITLE_BAR_HEIGHT' "$ROOT/src/config.h" || fail "config.h: title bar height missing"

for sym in windows_vector close_icon window_moving focused_window \
           window_autoincrement_id_current window_system_initialize \
           window_system_initialize_stage2 window_create; do
    grep -q "$sym" "$ROOT/src/graphics/window.c" || fail "window.c: $sym missing"
done

grep -q 'graphics_image_load("@:/clsicon.bmp")' "$ROOT/src/graphics/window.c" \
    || fail "window.c: clsicon.bmp load missing"

# window.o should not yet be in build.
if grep -q 'build/graphics/window.o' "$ROOT/Makefile"; then
    fail "Makefile: window.o should not be in FILES at L119"
fi

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L119 window source part 1"
