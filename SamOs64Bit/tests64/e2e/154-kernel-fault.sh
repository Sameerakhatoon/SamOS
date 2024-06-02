#!/usr/bin/env bash
# e2e/154-kernel-fault.sh - a fault in kernel mode, after
# idt_init, is a panic with a register dump. A fault that cannot push
# its frame (rsp unmapped) becomes a #DF, which runs on its own IST
# stack and dumps too instead of triple-faulting. Also checks the
# CR0/CR4/EFER line from cpu_init.
# Parity: on aarch64 and riscv64 the undefined instruction after trap
# init is dumped (ESR/ELR/FAR and x0-x30; scause/sepc/stval and the
# GPRs) and halts. aarch64 reports a fault on an unmapped sp from a
# fresh stack, its #DF; riscv64 has no separate fault stack
# (arch_fault_test), so that half runs on x86 and aarch64 only.
. "$(dirname "$0")/_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
set_cmdline() {
    printf '%s\n' "$1" > "$E2E_TMP/CMDLINE.TXT"
    MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
        || e2e_fail "mcopy CMDLINE.TXT"
}

case "$E2E_ARCH" in
aarch64) halted='^exception: (kernel stack overflow, )?halted$' ;;
riscv64) halted='^System halted\.$' ;;
*) halted='KERNEL PANIC' ;;
esac

set_cmdline samos.kernel_fault_test
boot_and_dump 20 "$halted"
case "$E2E_ARCH" in
aarch64)
    expect_serial '^Hello 64-bit!$' "the fault came after trap init"
    expect_serial '^exception: sync \(current EL, SP_ELx\)$' "sync exception at EL1 reported"
    expect_serial '^  ESR 0x2000000 \(EC 0x0, undefined instruction\)$' "ESR: EC 0x0, undefined instruction"
    expect_serial '^  ELR 0xffffffff800[0-9a-f]{5}$' "ELR in the higher-half kernel text"
    expect_serial '^  cause: undefined instruction 0x0000002a$' "the instruction (udf #0x2a)"
    expect_serial '^  x30 0x' "dump covers x0-x30"
    expect_serial '^exception: halted$' "kernel fault halts" ;;
riscv64)
    expect_serial '^Hello 64-bit!$' "the fault came after trap init"
    expect_serial '^KERNEL FAULT: scause 0x2 \(illegal instruction\)$' "illegal instruction in S-mode reported"
    expect_serial '^  sepc 0xffffffff802[0-9a-f]{5} stval 0x0 sstatus 0x[0-9a-f]+$' "sepc in the higher-half kernel text, stval the instruction (0)"
    expect_serial '^  t4 0x' "dump covers every GPR"
    expect_serial '^System halted\.$' "kernel fault halts" ;;
*)
    # cr0: PE, MP, ET (hardwired), NE, WP, PG set, EM and TS clear;
    # cr4: OSFXSR, OSXMMEXCPT; efer: LME, LMA, NXE
    expect_serial '^cpu: cr0 0x0000000080010033 cr4 0x[0-9a-f]{13}6[0-9a-f]{2} efer 0x0000000000000d00$' "cpu_init set CR0, CR4 and EFER"
    expect_serial '^KERNEL FAULT: #UD vector 0x0000000000000006 error 0x0000000000000000$' "#UD in kernel mode reported"
    expect_serial '^  rip 0xffffffff801[0-9a-f]{5} cs 0x0000000000000018 ' "register dump with a higher-half kernel rip and cs"
    expect_serial '^  r13 0x' "dump covers r8-r15"
    expect_serial 'KERNEL PANIC: CPU exception in kernel mode' "kernel fault panics"
    expect_no_serial 'EARLY FAULT' "the full IDT handled it, not the early one" ;;
esac

if [ "$E2E_ARCH" = riscv64 ]; then
    e2e_log "riscv64: no separate fault stack, double fault half not run"
    echo "PASS: e2e/154 kernel fault"
    exit 0
fi

set_cmdline samos.double_fault_test
boot_and_dump 20 "$halted"
case "$E2E_ARCH" in
aarch64)
    # The str to sp - 16 faults; the vector finds sp unusable and
    # reports from a fresh stack instead of faulting again.
    expect_serial '^exception: sync \(current EL, SP_ELx\) on a bad kernel stack, sp 0x7ff000000000$' "reported from a fresh stack with the bad sp"
    expect_serial '^  ESR 0x96000044 \(EC 0x25, data abort\)$' "the store's data abort (EC 0x25, WnR, translation fault)"
    expect_serial '^  FAR 0x7feffffffff0$' "FAR the store's address"
    expect_serial '^exception: kernel stack overflow, halted$' "halted" ;;
*)
    expect_serial '^KERNEL FAULT: #DF vector 0x0000000000000008 ' "#DF reported"
    expect_serial '^  rsp 0x00007ff000000000 ' "the #DF frame holds the bad rsp (handler ran on its IST stack)"
    expect_serial 'KERNEL PANIC: CPU exception in kernel mode' "double fault panics" ;;
esac
echo "PASS: e2e/154 kernel fault"
