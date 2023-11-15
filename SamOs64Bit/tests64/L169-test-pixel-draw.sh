#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# Re-assert the kernel-side ABI the L169 userland test exercises.
for sym in SYSTEM_COMMAND19_WINDOW_GRAPHICS_GET \
           SYSTEM_COMMAND20_GRAPHICS_PIXELS_BUFFER_GET \
           SYSTEM_COMMAND21_WINDOW_REDRAW; do
    grep -q "$sym" "$ROOT/src/isr80h/isr80h.h" \
        || fail "isr80h.h: $sym missing"
done

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L169 test pixel draw (kernel-side invariants)"
