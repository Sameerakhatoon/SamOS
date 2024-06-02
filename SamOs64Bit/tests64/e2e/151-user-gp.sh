#!/usr/bin/env bash
# e2e/151-user-gp.sh - a #GP in a user program (hlt in ring 3)
# kills only that program; BLANK.ELF still completes its selftest.
# Parity: aarch64 masks IRQs at EL0 (MSR DAIFSet with SCTLR_EL1.UMA
# clear traps as EC 0x18).
. "$(dirname "$0")/_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.run=KTEST.ELF gp\n' > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

boot_and_dump 30
case "$E2E_ARCH" in
aarch64)
    # ESR: EC 0x18, IL, the ISS of MSR DAIFSet, #2 (the same on every
    # run); FAR is not set for this class.
    expect_serial 'KTEST gp: masking IRQs at EL0$' "KTEST reached the msr"
    expect_serial '^USER FAULT: system register access in @:/KTEST.ELF: ESR 0x620793e4 ELR 0x4[0-9a-f]{5} FAR 0x[0-9a-f]+$' "system register trap reported with ESR and ELR" ;;
riscv64)
    # A privileged CSR read in U-mode; stval is the instruction
    # (csrr t0, sstatus).
    expect_serial 'KTEST gp: reading sstatus in U-mode$' "KTEST reached the csrr"
    expect_serial '^USER FAULT: scause 0x2 \(illegal instruction\) in @:/KTEST.ELF: sepc 0x4[0-9a-f]+ stval 0x100022f3$' "illegal instruction reported with sepc and stval" ;;
*)
    expect_serial 'KTEST gp: executing hlt in ring 3$' "KTEST reached the hlt"
    expect_serial '^USER FAULT: #GP in @:/KTEST.ELF: rip 0x00000000004[0-9a-f]+ error 0x0000000000000000 ' "#GP reported with rip and error code" ;;
esac
expect_serial '^process killed$' "faulting process killed"
expect_no_serial 'KTEST survived' "the faulting instruction did not continue"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT' "no kernel panic"
expect_stage_value 41 -eq 1 "BLANK.ELF fread after the kill"
echo "PASS: e2e/151 user #GP"
