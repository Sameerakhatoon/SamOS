#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# keyboard_init now lives BEFORE window_system_initialize_stage2.
kb_line=$(grep -n '^    keyboard_init();' "$ROOT/src/kernel.c" | head -1 | cut -d: -f1)
ws_line=$(grep -n '^    window_system_initialize_stage2();' "$ROOT/src/kernel.c" | head -1 | cut -d: -f1)
[ -n "$kb_line" ] || fail "kernel.c: live keyboard_init() call missing"
[ -n "$ws_line" ] || fail "kernel.c: window_system_initialize_stage2 call missing"
[ "$kb_line" -lt "$ws_line" ] || fail "kernel.c: keyboard_init must run before stage2"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L196 keyboard events userland fix"
