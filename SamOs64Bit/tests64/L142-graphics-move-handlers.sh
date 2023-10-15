#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

for sym in graphics_mouse_move graphics_mouse_move_handler graphics_move_handler_set; do
    grep -q "$sym" "$ROOT/src/graphics/graphics.c" || fail "graphics.c: $sym missing"
    grep -q "$sym" "$ROOT/src/graphics/graphics.h" || fail "graphics.h: $sym missing"
done

# Stage-2 init registers the move handler too.
awk '/^void grpahics_setup_stage_two/,/^}$/{print}' "$ROOT/src/graphics/graphics.c" \
    | grep -q 'mouse_register_move_handler' \
    || fail "grpahics_setup_stage_two: move handler registration missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L142 graphics move handlers"
