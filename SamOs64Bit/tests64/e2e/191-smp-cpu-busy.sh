#!/usr/bin/env bash
# e2e/191-smp-cpu-busy.sh - done criterion "a CPU-bound test with
# 8 processes keeps all 4 cores busy".
#
# KTEST.ELF cpubusy 8 6000 starts eight `burn` children, each spinning
# for 6 s, then sleeps (so it uses no CPU) and reads every CPU's busy
# and idle timer ticks (syscall 51) 1.5 s and 4.5 s in. A tick counts
# as busy when the CPU had a task current. Every CPU must be busy in
# at least 90 % of its ticks over those 3 s, and the children together
# must have run on all four CPUs (each returns the mask of CPUs it
# was seen on). Behaviour, not speed: a loaded host slows the ticks
# down, it does not make a CPU idle while eight tasks wait.
# e2e-requires: smp (four CPUs; aarch64 starts its secondary CPUs in the aarch64 SMP work)
. "$(dirname "$0")/_lib.sh"

export E2E_SMP=4
cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.run=KTEST.ELF cpubusy 8 6000\n' > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

_script() {
    e2e_wait_serial '^CPUBUSY (OK|FAIL)$' 600 || true
    sleep 1
    printf 'quit\n'
}
e2e_run_qemu 700 _script

expect_serial '^smp: 4 cpus online$' "four CPUs online"
log=$(tr -d '\r' < "$E2E_SERIAL")
for cpu in 0 1 2 3; do
    line=$(printf '%s\n' "$log" | grep -E "^CPUBUSY cpu $cpu busy" | head -n 1)
    [ -n "$line" ] || e2e_fail "no CPUBUSY line for cpu $cpu"
    pct=$(printf '%s\n' "$line" | sed -n 's/.* pct \([0-9]*\) .*/\1/p')
    busy=$(printf '%s\n' "$line" | sed -n 's/.* busy \([0-9]*\) .*/\1/p')
    [ "${busy:-0}" -gt 0 ] || e2e_fail "cpu $cpu counted no busy ticks: $line"
    [ "${pct:-0}" -ge 90 ] || e2e_fail "cpu $cpu busy only $pct %: $line"
    e2e_log "cpu $cpu busy $pct % ($line): ok"
done
expect_serial '^CPUBUSY 4 cpus, children together ran on 0xf$' "the children ran on all four CPUs"
expect_serial '^CPUBUSY OK$' "ktest says every CPU was busy"
expect_no_serial 'CPUBUSY .*FAIL|KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:' "no failure or fault"
echo "PASS: e2e/191 eight CPU-bound processes keep four CPUs busy"
