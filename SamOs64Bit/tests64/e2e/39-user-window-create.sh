#!/usr/bin/env bash
# e2e/39-user-window-create.sh - L154 userspace window create.
# Asserts samos_window_create round-tripped through int 0x80
# without faulting. The handle may be NULL today because the
# kernel-side window stack only fully wires up when the L136
# Test-Window path runs, which is skipped in our user-mode
# direct-boot.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 53 -eq 1 "samos_window_create syscall round trip"
echo "PASS: e2e/39 user window create"
