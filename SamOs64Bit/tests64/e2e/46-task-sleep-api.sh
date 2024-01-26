#!/usr/bin/env bash
# e2e/46-task-sleep-api.sh - L197 task_sleep entry-point is wired
# into the kernel API surface. We do not invoke it here (no task
# is current yet at kernel_main; deref would crash); the probe
# marks pass to assert the function symbol is reachable from
# kernel_selftest's link set. The end-to-end behaviour (sleep +
# wake) is covered by e2e/37-user-udelay.sh through ring 3.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 61 -eq 1 "task_sleep API surface present"
echo "PASS: e2e/46 task_sleep api"
