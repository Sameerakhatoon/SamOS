#!/usr/bin/env bash
# e2e/190-smp-boot.sh - done criterion "it boots with -smp 4",
# on pc and q35.
#
# Each boot: the BSP starts the three APs from the MADT list with
# INIT-SIPI-SIPI and the real-mode trampoline; every AP reaches long
# mode, its own GDT/TSS, GS base and LAPIC timer and reports online.
# Then the SMP self-test (marker 189): a cross-CPU call reaches each
# AP and finds its own struct cpu through GS; a kernel-half page
# remapped while every AP holds the old translation is read back new
# on every AP (TLB shootdown IPI); four CPUs at once lose no count
# under a spinlock or with atomic_inc. BLANK.ELF's user self-test and
# the shell then run with the four CPUs scheduling. QEMU's own `info
# cpus` lists four CPUs.
#
# aarch64 (QEMU virt, once): PSCI CPU_ON from the device tree's
# /cpus, each CPU named by its MPIDR; the remap reaches the others by
# broadcast TLB maintenance (tlbi ...is) instead of an IPI.
# e2e-requires: smp (four CPUs; aarch64 starts its secondary CPUs in the aarch64 SMP work, riscv64 its harts in the riscv64 SMP work)
. "$(dirname "$0")/_lib.sh"

export E2E_SMP=4
machines="pc q35"
[ "$E2E_ARCH" = aarch64 ] && machines="virt"
for machine in $machines; do
    E2E_MACHINE=$machine
    export E2E_MACHINE
    _script() {
        e2e_wait_serial '^e2e: user selftest done' 300 || true
        sleep 1
        printf 'info cpus\n'
        sleep 1
        e2e_pmemsave
        sleep 1
        printf 'quit\n'
    }
    e2e_run_qemu 400 _script
    e2e_log "machine $machine"
    for cpu in 1 2 3; do
        expect_serial "^smp: cpu $cpu (apic [0-9]+|mpidr 0x[0-9a-f]+) online$" "$machine: cpu $cpu online"
    done
    expect_serial '^smp: 4 cpus online$' "$machine: four CPUs online"
    expect_serial '^smp selftest: call, tlb shootdown, lock and atomic counts ok$' \
        "$machine: IPI call, TLB shootdown, locks across CPUs"
    expect_stage_value 188 -eq 4 "$machine: marker 188 (CPUs online)"
    expect_stage_value 189 -eq 1 "$machine: marker 189 (SMP self-test)"
    expect_serial '^e2e: user selftest done' "$machine: BLANK.ELF self-test finished"
    expect_no_serial 'did not start|KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:' "$machine: no fault"
    n=$(grep -ac 'CPU #' "$E2E_QEMU_LOG")
    [ "$n" -eq 4 ] || e2e_fail "$machine: QEMU info cpus lists $n CPUs, expected 4"
    e2e_log "$machine: QEMU lists 4 CPUs: ok"
done
echo "PASS: e2e/190 boots with -smp 4"
