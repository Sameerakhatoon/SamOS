#!/usr/bin/env bash
# e2e/163-sched-preempt-sleep.sh - done criteria "two CPU-bound
# programs interleave their output" and "a sleeping task wakes on
# time".
#
# Two KTEST.ELF spin tasks never make a blocking syscall (print does
# not yield), so their lines interleave only if the 100 Hz tick
# preempts them. A third task sleeps 500 ms while they spin and
# measures the sleep with uptime: it must not wake early, and must
# wake while both spinners still run (see the check at the end).
# The TSC line shows the CPUID max-leaf check and the PIT calibration.
. "$(dirname "$0")/_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.run=KTEST.ELF spin A 20;KTEST.ELF spin B 20;KTEST.ELF sleep 500\n' > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

_wait_all() {
    e2e_wait_serial '^SPIN A done' 300 || true
    e2e_wait_serial '^SPIN B done' 300 || true
    e2e_wait_serial '^SLEEP 500 ms took' 60 || true
    sleep 1
    printf 'quit\n'
}
e2e_run_qemu 700 _wait_all

expect_serial '^tsc: cpuid [0-9]+ kHz, pit [1-9][0-9]* kHz, using [1-9][0-9]* kHz$' \
    "TSC calibrated against the PIT, frequency not 0"
expect_serial '^SPIN A done ms [0-9]+$' "spinner A finished"
expect_serial '^SPIN B done ms [0-9]+$' "spinner B finished"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT|USER FAULT' "no fault"

log=$(tr -d '\r' < "$E2E_SERIAL")
line_of() { printf '%s\n' "$log" | grep -nx -- "$1" | head -n 1 | cut -d: -f1; }
a0=$(line_of 'SPIN A 0'); a3=$(line_of 'SPIN A 3')
b0=$(line_of 'SPIN B 0'); b3=$(line_of 'SPIN B 3')
[ -n "$a0" ] && [ -n "$a3" ] && [ -n "$b0" ] && [ -n "$b3" ] || e2e_fail "missing SPIN lines"
if [ "$b0" -lt "$a3" ] && [ "$a0" -lt "$b3" ]; then
    e2e_log "spinners interleave: A lines $a0..$a3, B lines $b0..$b3: ok"
else
    e2e_fail "spinners ran one after the other (A $a0..$a3, B $b0..$b3): no preemption"
fi

took=$(printf '%s\n' "$log" | sed -n 's/^SLEEP 500 ms took \([0-9]*\)$/\1/p' | head -n 1)
[ -n "$took" ] || e2e_fail "no SLEEP line"
# Never early. "On time" is checked by order, not by a bound in ms:
# under TCG on a loaded host the guest's ticks arrive late (675 and
# 1652 ms seen with load 22 on 12 cores), but a sleeper woken by the
# tick still wakes while both spinners run (each spins for seconds);
# without preemption or tick wakeups it would wake after them.
# With E2E_SMP each spinner has a CPU of its own: 4 rounds took
# 260-560 ms there, less than the sleep, hence 20 rounds.
[ "$took" -ge 500 ] || e2e_fail "sleep_ms(500) returned early, after $took ms"
s=$(printf '%s\n' "$log" | grep -n '^SLEEP 500 ms took' | head -n 1 | cut -d: -f1)
ad=$(printf '%s\n' "$log" | grep -n '^SPIN A done' | head -n 1 | cut -d: -f1)
bd=$(printf '%s\n' "$log" | grep -n '^SPIN B done' | head -n 1 | cut -d: -f1)
if [ "$s" -lt "$ad" ] && [ "$s" -lt "$bd" ]; then
    e2e_log "sleep_ms(500) woke after $took ms, while both spinners still ran: ok"
else
    e2e_fail "sleep_ms(500) woke only after a spinner finished ($took ms): no tick wakeup"
fi
echo "PASS: e2e/163 preemption and sleep"
