#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q '^void window_click(struct window\* window' "$ROOT/src/graphics/window.c" \
    || fail "window.c: real body missing"
grep -q 'WINDOW_EVENT_TYPE_MOUSE_CLICK' "$ROOT/src/graphics/window.c" \
    || fail "window.c: MOUSE_CLICK event type not used"
grep -q 'window_click(struct window\* window' "$ROOT/src/graphics/window.h" \
    || fail "window.h: prototype missing"
if grep -q 'weak.*window_click' "$ROOT/src/graphics/window.c"; then
    fail "window.c: weak window_click stub must be removed"
fi

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L144 window_click"
