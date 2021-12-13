#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# graphics_setup must call graphics_redraw_all at the end.
awk '/^void graphics_setup\(/,/^}$/{print}' "$ROOT/src/graphics/graphics.c" \
    | grep -q 'graphics_redraw_all()' \
    || fail "graphics.c: graphics_setup must finish by calling graphics_redraw_all"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L101 redraw black bg"
