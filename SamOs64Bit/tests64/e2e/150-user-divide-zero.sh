#!/usr/bin/env bash
# e2e/150-user-divide-zero.sh - a divide by zero in a user
# program kills only that program. KTEST.ELF runs first (CMDLINE.TXT
# samos.run=...), faults, and BLANK.ELF still runs its whole selftest
# afterwards (files, heap, windows), so the kernel kept going.
. "$(dirname "$0")/_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.run=KTEST.ELF de\n' > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

boot_and_dump 30
expect_serial '^samos.run: loaded @:/KTEST.ELF$' "KTEST.ELF loaded from the command line"
expect_serial '^KTEST de: dividing by zero$' "KTEST reached the divide"
expect_serial '^USER FAULT: #DE in @:/KTEST.ELF: rip 0x00000000004' "#DE reported with the user rip"
expect_serial '^process killed$' "faulting process killed"
expect_no_serial 'KTEST survived' "the faulting instruction did not continue"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT' "no kernel panic"
expect_stage_value 41 -eq 1 "BLANK.ELF fread after the kill"
expect_stage_value 46 -eq 1 "BLANK.ELF realloc after the kill"
echo "PASS: e2e/150 user divide by zero"
