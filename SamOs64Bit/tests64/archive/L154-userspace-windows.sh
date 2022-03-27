#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# struct process_window and process_userspace_window in process.h
grep -q 'struct process_userspace_window' "$ROOT/src/task/process.h" \
    || fail "process.h: process_userspace_window missing"
grep -q 'struct process_window' "$ROOT/src/task/process.h" \
    || fail "process.h: process_window missing"
grep -q 'struct vector\*\s\+windows' "$ROOT/src/task/process.h" \
    || fail "process.h: windows vector field missing"

# Helper decls land.
for sym in process_window_create process_owns_kernel_window \
           process_get_from_kernel_window process_window_get_from_user_window \
           process_close_windows; do
    grep -q "$sym" "$ROOT/src/task/process.h" || fail "process.h: $sym decl missing"
    grep -q "$sym" "$ROOT/src/task/process.c" || fail "process.c: $sym body missing"
done

# Process_init wires the windows vector.
grep -q 'process->windows =' "$ROOT/src/task/process.c" \
    || fail "process.c: windows vector init missing"

# process_terminate calls process_close_windows.
grep -q 'process_close_windows(process)' "$ROOT/src/task/process.c" \
    || fail "process.c: process_close_windows not invoked"

# window_close decl now exists in window.h.
grep -q '^void\s\+window_close' "$ROOT/src/graphics/window.h" \
    || fail "window.h: window_close decl missing"

# isr80h plumbing.
[ -f "$ROOT/src/isr80h/window.h" ] || fail "isr80h/window.h missing"
[ -f "$ROOT/src/isr80h/window.c" ] || fail "isr80h/window.c missing"
grep -q 'isr80h_command16_window_create' "$ROOT/src/isr80h/window.c" \
    || fail "isr80h/window.c: command16 missing"
grep -q 'SYSTEM_COMMAND16_WINDOW_CREATE' "$ROOT/src/isr80h/isr80h.h" \
    || fail "isr80h.h: SYSTEM_COMMAND16 missing"
grep -q 'SYSTEM_COMMAND16_WINDOW_CREATE' "$ROOT/src/isr80h/isr80h.c" \
    || fail "isr80h.c: command16 not registered"
grep -q 'build/isr80h/window.o' "$ROOT/Makefile" \
    || fail "Makefile: isr80h/window.o missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L154 userspace windows"
