#!/usr/bin/env bash
# e2e/141-q35-handoff.sh - the UEFI loader hands off to the
# kernel on q35 (AHCI, no IDE) as well as on pc, and the kernel boots
# all the way: boot_info accepted, memory map usable, the kernel
# selftest, and BLANK.ELF loaded from @: over AHCI through to the end
# of its user-side selftest.
#
# Until the usability work and the storage work this stopped at "register isr80h". The cause was
# not heap corruption: with no AHCI driver the system font was never
# loaded, so the selftest mouse had no window and mouse_position_set
# dereferenced NULL. The storage work added the AHCI driver and
# The NULL check in mouse_position_set.
E2E_MACHINE=q35
. "$(dirname "$0")/_lib.sh"

boot_and_dump 30
expect_serial '^SamOS: kernel_main$' "kernel entered on q35"
expect_serial '^boot_info: cmdline ""$' "boot_info magic and version accepted"
expect_serial 'multiheap ready$' "memory map usable on q35"
expect_serial 'register isr80h' "early boot finished on q35"
expect_serial '^e2e: running user-side BLANK.ELF selftest$' "kernel selftest done, BLANK.ELF loaded over AHCI"
expect_serial "$E2E_SENTINEL" "BLANK.ELF selftest finished on q35"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT|USER FAULT' "no fault on q35"
echo "PASS: e2e/141 q35 handoff"
