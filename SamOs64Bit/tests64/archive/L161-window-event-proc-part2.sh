#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

for sym in process_push_window_event process_pop_window_event; do
    grep -q "$sym" "$ROOT/src/task/process.h" || fail "process.h: $sym decl missing"
    grep -q "^int $sym" "$ROOT/src/task/process.c" || fail "process.c: $sym body missing"
done

# free path drops the ring vector.
grep -q 'vector_free(process->window_events.vector)' "$ROOT/src/task/process.c" \
    || fail "process.c: window_events vector_free missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L161 window event proc part 2"
