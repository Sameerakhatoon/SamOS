#!/usr/bin/env bash
# e2e/01-boot.sh - kernel reached kernel_main without triple-faulting.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 15
expect_stage_reached 1 "kernel_main entered"
echo "PASS: e2e/01 boot"
