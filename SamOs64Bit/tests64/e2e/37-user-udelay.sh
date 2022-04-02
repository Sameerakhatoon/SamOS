#!/usr/bin/env bash
# e2e/37-user-udelay.sh - userland udelay round trip (L198,
# SYSTEM_COMMAND25_UDELAY). The user task asks the kernel to put
# it to sleep for 100 us, then writes the marker on resume. If
# task_sleep + task_next + the IRQ-timer wakeup chain breaks,
# the marker stays zero.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 49 -eq 1 "samos_udelay(100) returns control to userland"
echo "PASS: e2e/37 user udelay"
