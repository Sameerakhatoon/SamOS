#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

for sym in process_window_event_get_relative_window_body_coords \
           process_window_event_modify_for_userspace_mouse_click \
           process_window_event_modify_for_userspace_mouse_move \
           process_window_event_modify_for_userspace \
           process_current_set \
           process_window_event_handler_event_focus \
           process_window_get_from_kernel_window \
           process_window_event_handler_event_close \
           process_window_event_handler_kernel_handle_event \
           process_window_event_handler \
           process_window_closed; do
    grep -q "$sym" "$ROOT/src/task/process.c" || fail "process.c: $sym missing"
done

# Event handler registered at window-create time.
grep -q 'window_event_handler_register(proc_win->kernel_win, process_window_event_handler)' \
    "$ROOT/src/task/process.c" \
    || fail "process.c: handler not registered in process_window_create"

# process_window_closed body actually cleans up.
grep -q 'vector_pop_element(process->windows' "$ROOT/src/task/process.c" \
    || fail "process.c: process_window_closed does not pop"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L162 window event proc part 3"
