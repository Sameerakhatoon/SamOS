#!/usr/bin/env bash
# e2e/141-q35-handoff.sh - the UEFI loader hands off to the
# kernel on q35 (AHCI, no IDE) as well as on pc. Checks the boot_info
# handoff and early boot up to the syscall table.
#
# Stops at "register isr80h": the kernel selftest that runs after it
# still faults on q35 (graphics child pointer corrupted; see
# the boot notes), which the memory manager work addresses.
E2E_MACHINE=q35
. "$(dirname "$0")/_lib.sh"

boot_and_dump 20 'register isr80h'
expect_serial '^SamOS: kernel_main$' "kernel entered on q35"
expect_serial '^boot_info: cmdline ""$' "boot_info magic and version accepted"
expect_serial 'multiheap ready$' "memory map usable on q35"
expect_serial 'register isr80h' "early boot finished on q35"
echo "PASS: e2e/141 q35 handoff"
