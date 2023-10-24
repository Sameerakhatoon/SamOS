#!/usr/bin/env bash
# e2e/151-user-gp.sh - a #GP in a user program (hlt in ring 3)
# kills only that program; BLANK.ELF still completes its selftest.
. "$(dirname "$0")/_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.run=KTEST.ELF gp\n' > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

boot_and_dump 30
expect_serial 'KTEST gp: executing hlt in ring 3$' "KTEST reached the hlt"
expect_serial '^USER FAULT: #GP in @:/KTEST.ELF: rip 0x00000000004[0-9a-f]+ error 0x0000000000000000 ' "#GP reported with rip and error code"
expect_serial '^process killed$' "faulting process killed"
expect_no_serial 'KTEST survived' "the faulting instruction did not continue"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT' "no kernel panic"
expect_stage_value 41 -eq 1 "BLANK.ELF fread after the kill"
echo "PASS: e2e/151 user #GP"
