#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# Typo rename landed.
grep -q 'bits_height_per_character' "$ROOT/src/graphics/font.h" \
    || fail "font.h: bits_height_per_character missing"
# Comments may mention the historic typo; the live code must not.
if grep -E '^[^/]*bits_height_per_chracter' "$ROOT/src/graphics/font.h" | grep -v '^[[:space:]]*//' > /dev/null; then
    fail "font.h: stale bits_height_per_chracter in code"
fi
if grep -E '^[^/]*bits_height_per_chracter' "$ROOT/src/graphics/font.c" | grep -v '^[[:space:]]*//' > /dev/null; then
    fail "font.c: stale bits_height_per_chracter in code"
fi

# Terminal API exports.
for sym in terminal_free terminal_background_save terminal_restore_background \
           terminal_get_at_screen_position terminal_cursor_set terminal_cursor_row \
           terminal_cursor_col terminal_total_cols terminal_total_rows; do
    grep -q "$sym" "$ROOT/src/graphics/terminal.h" \
        || fail "terminal.h: $sym not declared"
    grep -q "$sym" "$ROOT/src/graphics/terminal.c" \
        || fail "terminal.c: $sym not implemented"
done

# terminal.o now lands in build.
grep -q 'build/graphics/terminal.o' "$ROOT/Makefile" \
    || fail "Makefile: terminal.o not in FILES"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L97 terminal part 2"
