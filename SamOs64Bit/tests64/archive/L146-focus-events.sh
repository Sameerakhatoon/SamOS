#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# window_unfocus emits LOST_FOCUS.
awk '/^void window_unfocus/,/^}$/{print}' "$ROOT/src/graphics/window.c" > /tmp/uf.$$
grep -q 'WINDOW_EVENT_TYPE_LOST_FOCUS' /tmp/uf.$$ \
    || fail "unfocus: LOST_FOCUS event missing"
grep -q 'window_event_push' /tmp/uf.$$ \
    || fail "unfocus: window_event_push missing"
rm -f /tmp/uf.$$

# window_focus emits FOCUS.
awk '/^void window_focus/,/^}$/{print}' "$ROOT/src/graphics/window.c" > /tmp/f.$$
grep -q 'WINDOW_EVENT_TYPE_FOCUS' /tmp/f.$$ \
    || fail "focus: FOCUS event missing"
rm -f /tmp/f.$$

# TODO L146 must be gone.
if grep -q 'TODO L146' "$ROOT/src/graphics/window.c"; then
    fail "window.c: lingering TODO L146"
fi

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L146 focus events"
