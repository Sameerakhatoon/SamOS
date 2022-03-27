#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# Typo renames.
if grep -E '^[^/]*TRASPARENT' "$ROOT/src/graphics/graphics.c" "$ROOT/src/graphics/graphics.h" \
    | grep -v '^[^:]*:[[:space:]]*//' > /dev/null; then
    fail "TRASPARENT typo remains in code"
fi
if grep -E '^[^/]*grpahics_setup_stage_two' "$ROOT/src/graphics/graphics.c" "$ROOT/src/graphics/graphics.h" \
    | grep -v '^[^:]*:[[:space:]]*//' > /dev/null; then
    fail "grpahics_setup_stage_two typo remains in code"
fi
grep -q 'graphics_setup_stage_two' "$ROOT/src/graphics/graphics.h" \
    || fail "graphics_setup_stage_two decl missing"

# Real bug fixes.
grep -q 'graphics_info->transparency_key = pixel_color' "$ROOT/src/graphics/graphics.c" \
    || fail "transparency_key_set: still writes ignore_color"
grep -q 'intersect_right  = MIN(child_abs_right' "$ROOT/src/graphics/graphics.c" \
    || fail "intersect_right: still MAX"
grep -q 'width = g->width - local_x' "$ROOT/src/graphics/graphics.c" \
    || fail "graphics_redraw_region: still returns on overflow instead of clipping"

# Both branches use x >= for the left edge - check no stray `x >` survives.
if grep -E 'if\(x > child->starting_x' "$ROOT/src/graphics/graphics.c" > /dev/null; then
    fail "forward iteration: still uses x >"
fi

# Fallback uses starting_x not starting_y.
grep -q 'x < graphics->starting_x + graphics->width' "$ROOT/src/graphics/graphics.c" \
    || fail "fallback bounds check: starting_y still used"

# kernel.c calls graphics_setup_stage_two.
grep -q 'graphics_setup_stage_two(&default_graphics_info)' "$ROOT/src/kernel.c" \
    || fail "kernel.c: graphics_setup_stage_two missing"
grep -q 'mouse_system_load_static_drivers()' "$ROOT/src/kernel.c" \
    || fail "kernel.c: mouse_system_load_static_drivers missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L150 fix runtime bugs"
