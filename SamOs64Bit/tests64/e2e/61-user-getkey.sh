#!/usr/bin/env bash
# e2e/61-user-getkey.sh - L102 samos_getkey non-blocking round
# trip. We do not assert a specific key; the keyboard buffer is
# empty at the time of the call, so the return value is 0. What
# we lock down is the syscall completing without faulting.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 80 -eq 1 "samos_getkey() returns to userland"
echo "PASS: e2e/61 user getkey"
