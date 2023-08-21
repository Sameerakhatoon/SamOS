#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

for sym in terminal_write terminal_print terminal_backspace terminal_pixel_set \
           terminal_draw_image terminal_draw_rect \
           terminal_transparency_key_set terminal_transparency_key_remove \
           terminal_ignore_color terminal_ignore_color_finish; do
    grep -q "$sym" "$ROOT/src/graphics/terminal.h" \
        || fail "terminal.h: $sym not declared"
    grep -q "$sym" "$ROOT/src/graphics/terminal.c" \
        || fail "terminal.c: $sym not implemented"
done

# graphics_draw_rect stub.
grep -q 'graphics_draw_rect' "$ROOT/src/graphics/graphics.h" \
    || fail "graphics.h: draw_rect prototype missing"
grep -q 'void graphics_draw_rect(' "$ROOT/src/graphics/graphics.c" \
    || fail "graphics.c: draw_rect stub missing"

# VGA backspace got renamed to avoid collision.
grep -q 'vga_terminal_backspace' "$ROOT/src/kernel.c" \
    || fail "kernel.c: legacy backspace not renamed to vga_terminal_backspace"

# Backspace honours the flag in the write path.
grep -q '0x08' "$ROOT/src/graphics/terminal.c" \
    || fail "terminal.c: BS handler missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L98 terminal part 3"
