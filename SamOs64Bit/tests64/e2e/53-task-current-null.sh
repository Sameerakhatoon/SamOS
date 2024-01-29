#!/usr/bin/env bash
# e2e/53-task-current-null.sh - sentinel: task_current() is NULL
# before task_run_first_ever_task is called. Locks down the
# initial-state contract so a future use-after-free regression
# would surface as a non-NULL pointer here.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 69 -eq 1 "task_current() == NULL at kernel_main"
echo "PASS: e2e/53 task_current sentinel"
