#!/bin/bash
# tests/37-ring3-reached.sh
#
# Ch 98+99 - the supporting changes for ring-3 entry are wired:
#   - task_init sets registers.cs = USER_CODE_SEGMENT
#   - task_new sets current_task on the first task
#   - kernel_main reaches the launch point and prints
#     "entering userland... (deferred, G04)"
# The actual process_load + task_run_first_ever_task call is
# commented out pending the G04 investigation - see
# docs/gotchas/G04-iret-to-ring3-resets.md. This test asserts that
# the deferred-launch marker is visible on the VGA buffer, proving
# Ch 98's plumbing didn't break anything upstream.

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log"

chars=$(cat "$log")

if echo "$chars" | grep -q 'entering userland... (deferred, G04)'; then
    exit 0
fi

echo "FAIL: deferred-launch marker not on screen"
echo "      first 1200 chars: $(echo "$chars" | head -c 1200)"
exit 1
