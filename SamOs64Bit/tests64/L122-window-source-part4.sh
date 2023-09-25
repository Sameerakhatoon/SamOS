#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# Public API in header.
for sym in window_system_initialize window_system_initialize_stage2 \
           window_set_z_index window_unfocus window_focus window_create; do
    grep -q "$sym" "$ROOT/src/graphics/window.h" || fail "window.h: $sym missing"
done

# window_bring_to_top body.
grep -q '^void window_bring_to_top' "$ROOT/src/graphics/window.c" \
    || fail "window.c: window_bring_to_top body missing"
grep -q 'last_index + 1' "$ROOT/src/graphics/window.c" \
    || fail "window.c: bring_to_top must stamp last_index + 1"

# window.o lands in build.
grep -q 'build/graphics/window.o' "$ROOT/Makefile" \
    || fail "Makefile: window.o not in FILES"
grep -q '\./build/graphics/window\.o:' "$ROOT/Makefile" \
    || fail "Makefile: window.o recipe missing"

# kernel_main: init + stage2 + test window.
grep -q '#include "graphics/window.h"' "$ROOT/src/kernel.c" \
    || fail "kernel.c: window.h include missing"
grep -q 'window_system_initialize()' "$ROOT/src/kernel.c" \
    || fail "kernel.c: window_system_initialize missing"
grep -q 'window_system_initialize_stage2()' "$ROOT/src/kernel.c" \
    || fail "kernel.c: stage2 missing"
grep -q '"Test Window"' "$ROOT/src/kernel.c" \
    || fail "kernel.c: Test Window string missing"
grep -q 'window_create(graphics_screen_info()' "$ROOT/src/kernel.c" \
    || fail "kernel.c: window_create call missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L122 window source part 4"
