#!/usr/bin/env bash
# e2e/193-smp-panic-stop.sh - plan item "a panic IPI that stops
# all cores".
#
# samos.smp_panic_test: after the SMP self-test the BSP has CPU 1
# panic (a cross-CPU call) and spins. The panicking CPU sends an NMI
# to every other CPU; each one counts itself stopped and halts with
# interrupts off. Expect "panic: stopped 3 other cpus", the panic
# message, and QEMU's `info registers -a` showing all four CPUs halted
# (HLT=1), twice, two seconds apart, so none is still running.
. "$(dirname "$0")/_lib.sh"

export E2E_SMP=4
cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.smp_panic_test\n' > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

_script() {
    e2e_wait_serial 'KERNEL PANIC: smp panic test' 300 || true
    sleep 2
    printf 'info registers -a\n'
    sleep 2
    printf 'info registers -a\n'
    sleep 1
    printf 'quit\n'
}
e2e_run_qemu 400 _script

expect_serial '^smp: 4 cpus online$' "four CPUs online"
expect_serial '^smp: panic test on cpu 1$' "panic test started"
expect_serial '^panic: stopped 3 other cpus$' "the panicking CPU stopped the three others"
expect_serial 'KERNEL PANIC: smp panic test' "the panic message"
halted=$(grep -ao 'HLT=1' "$E2E_QEMU_LOG" | wc -l)
running=$(grep -ao 'HLT=0' "$E2E_QEMU_LOG" | wc -l)
[ "$halted" -eq 8 ] && [ "$running" -eq 0 ] \
    || e2e_fail "expected 4 CPUs halted in both dumps, got HLT=1 x$halted, HLT=0 x$running"
e2e_log "all four CPUs halted in both register dumps: ok"
echo "PASS: e2e/193 a panic stops every CPU"
