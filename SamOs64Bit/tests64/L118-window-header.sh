#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

[ -f "$ROOT/src/graphics/window.h" ] || fail "window.h missing"

# Macro lives in config.
grep -q 'WINDOW_MAX_TITLE' "$ROOT/src/config.h" || fail "config.h: WINDOW_MAX_TITLE missing"

# Core enums.
for ev in WINDOW_EVENT_TYPE_NULL WINDOW_EVENT_TYPE_FOCUS \
          WINDOW_EVENT_TYPE_LOST_FOCUS WINDOW_EVENT_TYPE_MOUSE_MOVE \
          WINDOW_EVENT_TYPE_MOUSE_CLICK WINDOW_EVENT_TYPE_WINDOW_CLOSE \
          WINDOW_EVENT_TYPE_KEY_PRESS; do
    grep -q "$ev" "$ROOT/src/graphics/window.h" || fail "window.h: $ev missing"
done
for fl in WINDOW_FLAG_BORDERLESS WINDOW_FLAG_CLICK_THROUGH \
          WINDOW_FLAG_BACKGROUND_TRANSPARENT; do
    grep -q "$fl" "$ROOT/src/graphics/window.h" || fail "window.h: $fl missing"
done

# Key structs and typedef.
grep -q 'struct window_event' "$ROOT/src/graphics/window.h" || fail "window_event missing"
grep -q 'WINDOW_EVENT_HANDLER' "$ROOT/src/graphics/window.h" || fail "handler typedef missing"
grep -q 'struct window {' "$ROOT/src/graphics/window.h" || fail "struct window missing"
grep -q 'title_bar_terminal' "$ROOT/src/graphics/window.h" || fail "title_bar_terminal field missing"
grep -q 'root_graphics' "$ROOT/src/graphics/window.h" || fail "root_graphics field missing"
grep -q 'close_btn' "$ROOT/src/graphics/window.h" || fail "close_btn shape missing"

# window.c must NOT exist at L118 yet.
if [ -f "$ROOT/src/graphics/window.c" ]; then
    fail "window.c should not yet exist at L118"
fi

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L118 window header"
