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
#
# aarch64 stops them with an FIQ (a Group 0 SGI); QEMU's
# dump has no halted flag, so every CPU must have DAIF masked and its
# pc in aarch64_halt's wfi loop in both dumps.
#
# riscv64 has no NMI (an SBI IPI, and a poll in cpu_relax) and
# QEMU's dump has no halted flag: every hart, the panicking one too,
# parks in one wfi loop (riscv64_park) with sstatus.SIE and the S bits
# of sie clear, so all eight pcs must be the same address, and
# mstatus bit 1 (SIE) and mie bits 1, 5, 9 clear in every dump.
# e2e-requires: smp (four CPUs; aarch64 starts its secondary CPUs in the aarch64 SMP work, riscv64 its harts in the riscv64 SMP work)
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
if [ "$E2E_ARCH" = aarch64 ]; then
    # No HLT flag either: a CPU stopped in aarch64_halt waits in
    # its wfi loop with D, A, I and F all masked (PSTATE 0x3c0). A
    # pending (masked) interrupt still ends a wfi, so the pc may be
    # anywhere in that loop: inside aarch64_halt, in both dumps.
    regs=$(tr -d '\r' < "$E2E_QEMU_LOG")
    masked=0
    for ps in $(printf '%s\n' "$regs" | sed -n 's/.*PSTATE=\([0-9a-f]*\).*/\1/p'); do
        [ $(( 0x$ps & 0x3c0 )) -eq $(( 0x3c0 )) ] && masked=$((masked + 1))
    done
    read -r halt size _ < <(aarch64-linux-gnu-nm -S "$E2E_ROOT/build/aarch64/kernel.elf" | awk '$4 == "aarch64_halt"')
    inside=0 n=0
    for pc in $(printf '%s\n' "$regs" | grep -aoE '(^|[^A-Z_0-9])PC=[0-9a-f]+' | sed 's/.*=//'); do
        n=$((n + 1))
        [ $((0x$pc)) -ge $((0x${halt:-1})) ] && [ $((0x$pc)) -lt $((0x${halt:-1} + 0x${size:-0})) ] && inside=$((inside + 1))
    done
    [ "$n" -eq 8 ] && [ "$masked" -eq 8 ] && [ "$inside" -eq 8 ] \
        || e2e_fail "expected 4 CPUs in aarch64_halt (0x$halt, 0x$size bytes) with DAIF masked in both dumps: $n pcs, $masked masked, $inside inside"
elif [ "$E2E_ARCH" = riscv64 ]; then
    reg() { tr -d '\r' < "$E2E_QEMU_LOG" | sed -n "s/^ *$1  *\([0-9a-f]*\)$/\1/p"; }
    pcs=$(reg pc | wc -l)
    distinct=$(reg pc | sort -u | wc -l)
    on=0
    for v in $(reg mstatus); do [ $(( 0x$v & 0x2 )) -eq 0 ] || on=$((on + 1)); done
    for v in $(reg mie); do [ $(( 0x$v & 0x222 )) -eq 0 ] || on=$((on + 1)); done
    [ "$pcs" -eq 8 ] && [ "$distinct" -eq 1 ] && [ "$on" -eq 0 ] \
        || e2e_fail "expected 4 harts parked at one pc in both dumps, got $pcs pcs, $distinct distinct, $on with interrupts on"
else
    halted=$(grep -ao 'HLT=1' "$E2E_QEMU_LOG" | wc -l)
    running=$(grep -ao 'HLT=0' "$E2E_QEMU_LOG" | wc -l)
    [ "$halted" -eq 8 ] && [ "$running" -eq 0 ] \
        || e2e_fail "expected 4 CPUs halted in both dumps, got HLT=1 x$halted, HLT=0 x$running"
fi
e2e_log "all four CPUs halted in both register dumps: ok"
echo "PASS: e2e/193 a panic stops every CPU"
