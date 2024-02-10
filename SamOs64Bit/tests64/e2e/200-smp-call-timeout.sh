#!/usr/bin/env bash
# e2e/200-smp-call-timeout.sh - a cross-CPU call wait is
# bounded like a lock wait (docs/locking.md).
#
# samos.smp_call_ignore: CPU 1 drops every cross-CPU call, so the SMP
# self-test's first smp_call to it gets no answer. After 30 s
# (SAMOS_LOCK_TIMEOUT_US) the BSP panics naming itself and CPU 1, and
# the panic stops the other three CPUs. Before the timeout it spun forever.
. "$(dirname "$0")/_lib.sh"

export E2E_SMP=4
cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.smp_call_ignore\n' > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

_script() {
    e2e_wait_serial 'KERNEL PANIC: smp: a CPU did not answer a call' 300 || true
    sleep 1
    printf 'quit\n'
}
e2e_run_qemu 400 _script

expect_serial '^smp: 4 cpus online$' "four CPUs online"
expect_serial '^smp: cpu 1 ignores cross-CPU calls \(samos.smp_call_ignore\)$' "fault injected"
expect_serial '^panic: stopped 3 other cpus$' "the waiting CPU stopped the three others"
expect_serial '^smp: cpu 0 timed out waiting for cpu 1 to run a cross-CPU call$' "the report names both CPUs"
expect_serial 'KERNEL PANIC: smp: a CPU did not answer a call' "the panic message"
expect_no_serial '^smp selftest: call' "the self-test did not get past the first call"
echo "PASS: e2e/200 a cross-CPU call wait times out"
