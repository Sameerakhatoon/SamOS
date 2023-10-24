#!/usr/bin/env bash
# e2e/152-user-pf.sh - a #PF in a user program (write to an
# unmapped address) kills only that program, and the report carries
# the error code and CR2. BLANK.ELF still completes its selftest.
. "$(dirname "$0")/_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.run=KTEST.ELF pf\n' > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

boot_and_dump 30
expect_serial 'KTEST pf: writing to an unmapped page$' "KTEST reached the store"
# error 6 = user-mode write to a not-present page
expect_serial '^USER FAULT: #PF in @:/KTEST.ELF: rip 0x00000000004[0-9a-f]+ error 0x0000000000000006 cr2 0x0000700000000000$' "#PF reported with error code and cr2"
expect_serial '^process killed$' "faulting process killed"
expect_no_serial 'KTEST survived' "the faulting instruction did not continue"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT' "no kernel panic"
expect_stage_value 41 -eq 1 "BLANK.ELF fread after the kill"
echo "PASS: e2e/152 user #PF"
