#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q '^void graphics_info_recalculate(struct graphics_info' \
    "$ROOT/src/graphics/graphics.c" \
    || fail "graphics.c: body missing"
grep -q 'graphics_info_recalculate(struct graphics_info' "$ROOT/src/graphics/graphics.h" \
    || fail "graphics.h: prototype missing"

# Recursive call must be present.
awk '/^void graphics_info_recalculate/,/^}$/{print}' "$ROOT/src/graphics/graphics.c" \
    | grep -q 'graphics_info_recalculate(child_graphics_info)' \
    || fail "graphics.c: recursion call missing"

# L134 weak stub for graphics_info_recalculate retired.
if grep -q 'weak.*graphics_info_recalculate' "$ROOT/src/graphics/window.c"; then
    fail "window.c: L134 weak graphics_info_recalculate stub must be removed"
fi

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L135 graphics_info_recalculate"
