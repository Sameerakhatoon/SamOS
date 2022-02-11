#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# L164 kernel invariants: correct vector_free spelling, forward
# decls in place, intptr_t cast on the return path.
# Reject any vector_Free CALL (not the comment that documents it).
grep -E '^\s*vector_Free\s*\(' "$ROOT/src/task/process.c" \
    && fail "process.c: stale vector_Free typo present" || true
grep -q 'vector_free(process->window_events.vector)' "$ROOT/src/task/process.c" \
    || fail "process.c: window_events vector_free missing"
grep -q 'struct window_event;' "$ROOT/src/task/process.h" \
    || fail "process.h: window_event forward decl missing"
grep -q '(void\*)(intptr_t)res' "$ROOT/src/isr80h/window.c" \
    || fail "isr80h/window.c: command 18 missing intptr_t return cast"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L164 userspace get event call (kernel-side invariants)"
