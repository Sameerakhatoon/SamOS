#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q '^void window_redraw(struct window\* window)' "$ROOT/src/graphics/window.c" \
    || fail "window.c: real window_redraw missing"
grep -q 'graphics_redraw(window->root_graphics)' "$ROOT/src/graphics/window.c" \
    || fail "window.c: window_redraw must call graphics_redraw"
grep -q 'window_redraw(struct window\* window)' "$ROOT/src/graphics/window.h" \
    || fail "window.h: window_redraw decl missing"

if grep -q 'weak.*window_redraw' "$ROOT/src/graphics/window.c"; then
    fail "window.c: weak window_redraw stub must be gone"
fi

# kernel.c test window.
grep -q '"Test Window"' "$ROOT/src/kernel.c" || fail "kernel.c: Test Window literal missing"
grep -q 'WIndow creation problem' "$ROOT/src/kernel.c" \
    || fail "kernel.c: upstream WIndow typo not preserved"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L136 window_redraw"
