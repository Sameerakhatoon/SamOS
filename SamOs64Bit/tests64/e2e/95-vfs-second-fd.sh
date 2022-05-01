#!/usr/bin/env bash
# e2e/95-vfs-second-fd.sh - L48 fopen the same file twice yields
# two distinct file descriptors.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 127 -eq 1 "two fopen calls on @:/BLANK.ELF yield distinct fds"
echo "PASS: e2e/95 vfs second fd"
