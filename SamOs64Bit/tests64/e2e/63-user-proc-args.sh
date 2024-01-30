#!/usr/bin/env bash
# e2e/63-user-proc-args.sh - L115 samos_process_get_arguments
# round trip. argc may be 0 when BLANK.ELF is launched by the
# kernel directly (no arg-passing happened); we just assert the
# syscall returned to userland without faulting.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 84 -eq 1 "samos_process_get_arguments round trip"
echo "PASS: e2e/63 user proc_args"
