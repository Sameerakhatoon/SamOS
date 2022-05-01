#!/usr/bin/env bash
# e2e/99-task-get-next-empty.sh - L66 sentinel: task_get_next
# returns NULL when the task ring is empty.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 131 -eq 1 "task_get_next() == NULL when task ring is empty"
echo "PASS: e2e/99 task_get_next empty"
