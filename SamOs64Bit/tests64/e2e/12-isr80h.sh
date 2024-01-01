#!/usr/bin/env bash
# e2e/12-isr80h.sh - isr80h_register_commands ran. Marker value
# is BM_STAGE_MAX-1 which we can sanity-check here without baking
# the constant into bash.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 20
expect_stage_reached 12 "isr80h registered"
expect_stage_value   12 -gt 0 "syscall table populated"
echo "PASS: e2e/12 isr80h"
