#!/usr/bin/env bash
# e2e/140-early-fault.sh - a CPU exception before idt_init()
# lands in the early IDT and dumps registers on COM1 instead of
# resetting the machine. CMDLINE.TXT on the ESP carries
# samos.early_fault_test, which makes kernel_main execute ud2.
# Parity: aarch64 and riscv64 install their one trap vector at boot
# (VBAR_EL1, stvec), so before arch_trap_init the same dump catches
# the undefined instruction (ESR/ELR and x0-x30; scause/sepc/stval
# and the GPRs) and the kernel halts there too.
. "$(dirname "$0")/_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.early_fault_test\n' > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

case "$E2E_ARCH" in
aarch64)
    boot_and_dump 20 '^exception: halted$'
    expect_serial '^boot_info: cmdline "samos.early_fault_test"$' "-append passed the command line"
    expect_serial '^exception: sync \(current EL, SP_ELx\)$' "sync exception at EL1 reported"
    expect_serial '^  ESR 0x2000000 \(EC 0x0, undefined instruction\)$' "ESR: EC 0x0, undefined instruction"
    expect_serial '^  ELR 0xffffffff800[0-9a-f]{5}$' "ELR in the higher-half kernel text"
    expect_serial '^  x30 0x' "dump covers x0-x30"
    expect_serial '^exception: halted$' "halted" ;;
riscv64)
    boot_and_dump 20 '^System halted\.$'
    expect_serial '^boot_info: cmdline "samos.early_fault_test"$' "-append passed the command line"
    expect_serial '^KERNEL FAULT: scause 0x2 \(illegal instruction\)$' "illegal instruction reported"
    expect_serial '^  sepc 0xffffffff802[0-9a-f]{5} stval 0x0 sstatus 0x[0-9a-f]+$' "sepc in the higher-half kernel text"
    expect_serial '^  t4 0x' "dump covers every GPR"
    expect_serial '^System halted\.$' "halted" ;;
*)
    boot_and_dump 20 '^System halted'
    expect_serial '^boot_info: cmdline "samos.early_fault_test"$' "loader passed the command line"
    expect_serial '^EARLY FAULT: vector 0x0000000000000006 ' "#UD reported by the early IDT"
    expect_serial '^  rip 0xffffffff801' "register dump with a kernel rip (higher half)" ;;
esac
expect_no_serial 'Hello 64-bit' "kernel stopped at the fault"
echo "PASS: e2e/140 early fault"
