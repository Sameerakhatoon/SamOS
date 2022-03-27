#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# Sysout window field on process.
grep -q 'struct process_window\* sysout_win' "$ROOT/src/task/process.h" \
    || fail "process.h: sysout_win field missing"

# Helpers declared and defined.
for sym in process_print_char process_print process_set_sysout_window; do
    grep -q "$sym" "$ROOT/src/task/process.h" || fail "process.h: $sym decl missing"
    grep -q "$sym" "$ROOT/src/task/process.c" || fail "process.c: $sym body missing"
done

# io.c print and putchar now use the process helpers.
grep -q 'process_print(task_current()->process' "$ROOT/src/isr80h/io.c" \
    || fail "isr80h/io.c: command1 not routed through process_print"
grep -q 'process_print_char(task_current()->process' "$ROOT/src/isr80h/io.c" \
    || fail "isr80h/io.c: command3 not routed through process_print_char"

# isr80h plumbing.
grep -q 'SYSTEM_COMMAND17_SYSOUT_TO_WINDOW' "$ROOT/src/isr80h/isr80h.h" \
    || fail "isr80h.h: command 17 missing"
grep -q 'SYSTEM_COMMAND17_SYSOUT_TO_WINDOW' "$ROOT/src/isr80h/isr80h.c" \
    || fail "isr80h.c: command 17 not registered"
grep -q 'isr80h_command17_sysout_to_window' "$ROOT/src/isr80h/window.c" \
    || fail "window.c: command 17 body missing"
grep -q 'isr80h_command17_sysout_to_window' "$ROOT/src/isr80h/window.h" \
    || fail "window.h: command 17 decl missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L158 stdout to window"
