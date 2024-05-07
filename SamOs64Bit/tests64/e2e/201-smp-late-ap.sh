#!/usr/bin/env bash
# e2e/201-smp-late-ap.sh - an AP that answers after the BSP
# gave up on it parks, and the next processor gets the slot alone.
#
# samos.smp_late_ap: the BSP gives up on the first AP (APIC 1) before
# its SIPIs, as if its 5 s window had passed. The AP arrives anyway,
# loses the start-up handshake, parks and writes only PARKED in its
# own slot; the BSP sends it INIT and starts APIC 2 and 3 in slots 1
# and 2. Before the start handshake the late AP would have run on with the slot and
# stack of the next processor. Then the SMP self-test and BLANK.ELF's
# self-test run on the three CPUs.
# e2e-requires: smp (four CPUs; aarch64 starts its secondary CPUs in the aarch64 SMP work, riscv64 its harts in the riscv64 SMP work)
. "$(dirname "$0")/_lib.sh"

export E2E_SMP=4
cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.smp_late_ap\n' > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

_script() {
    e2e_wait_serial '^e2e: user selftest done' 300 || true
    sleep 1
    e2e_pmemsave
    sleep 1
    printf 'quit\n'
}
e2e_run_qemu 400 _script

expect_serial '^smp: apic 1 did not start \(samos.smp_late_ap\)$' "the BSP gave up on APIC 1"
expect_serial '^smp: apic 1 answered late and parked$' "the late AP parked"
expect_no_serial '^smp: cpu [0-9]+ apic 1 online$' "the late AP never came online"
expect_serial '^smp: cpu 1 apic 2 online$' "APIC 2 took slot 1"
expect_serial '^smp: cpu 2 apic 3 online$' "APIC 3 took slot 2"
expect_serial '^smp: 3 cpus online$' "three CPUs online"
expect_stage_value 188 -eq 3 "marker 188 (CPUs online)"
expect_serial '^smp selftest: call, tlb shootdown, lock and atomic counts ok$' "SMP self-test on three CPUs"
expect_stage_value 189 -eq 1 "marker 189 (SMP self-test)"
expect_serial '^e2e: user selftest done' "BLANK.ELF self-test finished"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:' "no fault"
echo "PASS: e2e/201 a late AP parks"
