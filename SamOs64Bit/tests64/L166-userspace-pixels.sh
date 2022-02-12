#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q 'paging_align_value_to_upper_page' "$ROOT/src/memory/paging/paging.h" \
    || fail "paging.h: align helper decl missing"
grep -q '^uint64_t paging_align_value_to_upper_page' "$ROOT/src/memory/paging/paging.c" \
    || fail "paging.c: align helper body missing"

for sym in process_map_into_userspace process_map_graphics_framebuffer_pixels_into_userspace; do
    grep -q "$sym" "$ROOT/src/task/process.h" || fail "process.h: $sym decl missing"
    grep -q "$sym" "$ROOT/src/task/process.c" || fail "process.c: $sym body missing"
done

grep -q 'SYSTEM_COMMAND20_GRAPHICS_PIXELS_BUFFER_GET' "$ROOT/src/isr80h/isr80h.h" \
    || fail "isr80h.h: command 20 missing"
grep -q 'SYSTEM_COMMAND20_GRAPHICS_PIXELS_BUFFER_GET' "$ROOT/src/isr80h/isr80h.c" \
    || fail "isr80h.c: command 20 not registered"
grep -q 'isr80h_command20_graphics_pixels_get' "$ROOT/src/isr80h/graphics.c" \
    || fail "graphics.c: command 20 body missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L166 userspace pixels"
