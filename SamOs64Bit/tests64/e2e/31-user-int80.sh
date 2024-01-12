#!/usr/bin/env bash
# e2e/31-user-int80.sh - prove ring 3 is reachable end to end.
# The kernel loads BLANK.ELF (which has been extended into our
# user-side selftest harness, see programs/blank/blank.c). Slot
# 48 is the canary the user program writes FIRST: if it lands,
# we know:
#   * process_load_switch parsed BLANK.ELF and built a task
#   * the iretq into ring 3 succeeded
#   * the user-mode int 0x80 syscall path is alive
#   * SYSTEM_COMMAND26_E2E_MARK reached the kernel handler
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 48 -eq 1 "int 0x80 reached kernel from ring 3"
echo "PASS: e2e/31 user int80"
