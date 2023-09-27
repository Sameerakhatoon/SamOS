#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

for sym in window_event_handler_register window_event_handler_unregister \
           window_drop_event_handlers window_free window_event_push \
           window_close window_event_handler; do
    grep -q "$sym" "$ROOT/src/graphics/window.c" || fail "window.c: $sym missing"
done

# Upstream typo preserved.
grep -q 'vecotr_at' "$ROOT/src/graphics/window.c" \
    || fail "window.c: upstream typo vecotr_at should be preserved"

# Public API in header.
grep -q 'window_event_handler_register' "$ROOT/src/graphics/window.h" \
    || fail "window.h: register decl missing"
grep -q 'window_event_handler_unregister' "$ROOT/src/graphics/window.h" \
    || fail "window.h: unregister decl missing"

# Default handler is registered inside window_create.
grep -q 'window_event_handler_register(window, window_event_handler)' \
    "$ROOT/src/graphics/window.c" \
    || fail "window.c: default handler not registered inside window_create"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L123 window source part 5"
