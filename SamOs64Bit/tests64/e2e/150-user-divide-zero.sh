#!/usr/bin/env bash
# e2e/150-user-divide-zero.sh - a divide by zero in a user
# program kills only that program. KTEST.ELF runs first (CMDLINE.TXT
# samos.run=...), faults, and BLANK.ELF still runs its whole selftest
# afterwards (files, heap, windows), so the kernel kept going.
# Parity: aarch64 and riscv64 have no divide trap (the quotient is
# defined), so there KTEST.ELF ud runs the same check with the
# instruction fault each arch does have: an undefined instruction
# (aarch64 EC 0x0, riscv64 scause 2), x86's #UD.
. "$(dirname "$0")/_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
case "$E2E_ARCH" in
x86_64) mode=de ;;
*) mode=ud ;;
esac
printf 'samos.run=KTEST.ELF %s\n' "$mode" > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

boot_and_dump 30
expect_serial '^samos.run: loaded @:/KTEST.ELF$' "KTEST.ELF loaded from the command line"
case "$E2E_ARCH" in
aarch64)
    expect_serial 'KTEST ud: udf$' "KTEST reached the udf"
    expect_serial '^USER FAULT: undefined instruction in @:/KTEST.ELF: ESR 0x2000000 ELR 0x4[0-9a-f]{5} FAR 0x[0-9a-f]+$' "undefined instruction (EC 0x0) reported with the user ELR" ;;
riscv64)
    expect_serial 'KTEST ud: illegal instruction$' "KTEST reached the illegal instruction"
    expect_serial '^USER FAULT: scause 0x2 \(illegal instruction\) in @:/KTEST.ELF: sepc 0x4[0-9a-f]{5} stval 0x0$' "illegal instruction reported with sepc and stval (the instruction, 0)" ;;
*)
    expect_serial 'KTEST de: dividing by zero$' "KTEST reached the divide"
    expect_serial '^USER FAULT: #DE in @:/KTEST.ELF: rip 0x00000000004' "#DE reported with the user rip" ;;
esac
expect_serial '^process killed$' "faulting process killed"
expect_no_serial 'KTEST survived' "the faulting instruction did not continue"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT' "no kernel panic"
expect_stage_value 41 -eq 1 "BLANK.ELF fread after the kill"
expect_stage_value 46 -eq 1 "BLANK.ELF realloc after the kill"
echo "PASS: e2e/150 user divide by zero"
