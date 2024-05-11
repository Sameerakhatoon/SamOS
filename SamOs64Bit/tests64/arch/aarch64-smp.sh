#!/usr/bin/env bash
# aarch64 SMP on -machine virt,gic-version=3 -cpu cortex-a72
# -m 512M, -kernel with QEMU's device tree and the RAM disk (-initrd).
# The UEFI boots (AAVMF, ACPI with -smp 4, EL2 with -smp 2) are in
# aarch64-uefi.sh; the shared e2e SMP tests (190-193, 200, 201) run on
# aarch64 through the e2e hook.
#   smp1:   -smp 1: one CPU, nothing to start, the kernel runs as before.
#   smp2:   -smp 2: the second CPU from /cpus started by PSCI CPU_ON
#           (hvc), the SMP self-test.
#   smp4:   -smp 4: three CPUs online with their MPIDRs, in /cpus order;
#           the SMP self-test: a cross-CPU call (an SGI there and the
#           answer back) to each CPU that finds its own struct cpu
#           through TPIDR_EL1, a kernel page remapped under every CPU
#           read back new on each (broadcast tlbi, no IPI), and lock
#           and atomic counts from four CPUs at once; then KTEST.ELF
#           cpubusy: every CPU counts busy timer ticks of its own and
#           eight CPU-bound children run on all four.
#   el2:    virtualization=on, -smp 4: PSCI over smc, the secondaries
#           start at EL2 and drop to EL1 as the boot CPU does.
#   nogicr: an extra /cpus node, MPIDR 0x7, that no CPU and no GIC
#           redistributor has: reported and skipped, the other three
#           still start in slots 1-3.
#   dup:    a second node for MPIDR 0x1 before the real one: the first
#           starts it, CPU_ON on the second returns ALREADY_ON (-4),
#           which is reported and skipped; four CPUs online.
#   panic:  samos.smp_panic_test: CPU 1 panics, the FIQ stop halts the
#           three others ("panic: stopped 3 other cpus").
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
timeout_s="${ARCH_TIMEOUT:-180}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

checks=0
failures=0

bash "$root/arch-build.sh" aarch64 > "$tmp/build.log" 2>&1 || {
    cat "$tmp/build.log"
    echo "FAIL: aarch64 build"
    echo "aarch64-smp arch test: 1 checks, 1 failures"
    exit 1
}

# boot <name> <machine options> <qemu args> <append> <ERE>... : until
# every ERE matched (QEMU with several vCPUs runs one thread each).
boot() {
    local name="$1" machine="$2" args="$3" append="$4"
    shift 4
    local log="$tmp/$name.log"
    : > "$log"
    # shellcheck disable=SC2086
    qemu-system-aarch64 -machine "virt,gic-version=3$machine" -cpu cortex-a72 -m 512M -nographic \
        -accel tcg,thread=multi -kernel "$root/bin/aarch64/kernel.img" \
        -initrd "$root/bin/aarch64/ramdisk.img" -append "$append" $args \
        < /dev/null > "$log" 2>&1 &
    local pid=$! waited=0
    while kill -0 "$pid" 2>/dev/null; do
        local all=1 re
        tr -d '\r' < "$log" > "$log.now"
        for re in "$@"; do
            grep -aqE "$re" "$log.now" || { all=0; break; }
        done
        [ $all -eq 1 ] && break
        if [ $waited -ge $((timeout_s * 10)) ]; then
            echo "  ($name: timed out after ${timeout_s}s)"
            break
        fi
        sleep 0.1
        waited=$((waited + 1))
    done
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
    tr -d '\r' < "$log" > "$log.txt"
}

result() {
    checks=$((checks + 1))
    if [ "$1" -eq 0 ]; then
        echo "PASS: $2"
    else
        failures=$((failures + 1))
        echo "FAIL: $2"
    fi
}

expect() {
    if grep -aqE "$2" "$tmp/$1.log.txt"; then
        result 0 "$1: $3"
    else
        result 1 "$1: $3 (no match for /$2/)"
    fi
}

expect_no() {
    if grep -aqE "$2" "$tmp/$1.log.txt"; then
        result 1 "$1: $3 (unexpected /$2/)"
    else
        result 0 "$1: $3"
    fi
}

no_crash() {
    expect_no "$1" '^exception:|KERNEL PANIC|^trap:|^irq: unexpected|spinlock:|USER FAULT' "no exception, panic, stray IRQ or lock timeout"
}

selftest_ok='^smp selftest: call, tlb shootdown, lock and atomic counts ok$'
done_re='^e2e: user selftest done$'

# ---- one, two, four CPUs ----------------------------------------------
boot smp1 "" "-smp 1" "samos.run=HELLO.ELF" "$done_re"
expect smp1 '^smp: 1 cpus online$' "one CPU online"
expect_no smp1 '^smp: cpu [0-9]+ mpidr' "no other CPU started"
expect smp1 "$done_re" "BLANK.ELF's self-test"
no_crash smp1

boot smp2 "" "-smp 2" "samos.run=HELLO.ELF" "$done_re"
expect smp2 '^smp: cpu 1 mpidr 0x1 online$' "CPU 1 (MPIDR 0x1) started by PSCI CPU_ON"
expect smp2 '^smp: 2 cpus online$' "two CPUs online"
expect smp2 "$selftest_ok" "SMP self-test"
expect smp2 "$done_re" "BLANK.ELF's self-test"
no_crash smp2

boot smp4 "" "-smp 4" "samos.run=KTEST.ELF cpubusy 8 4000" '^CPUBUSY (OK|FAIL)$'
for cpu in 1 2 3; do
    expect smp4 "^smp: cpu $cpu mpidr 0x$cpu online\$" "CPU $cpu online with MPIDR 0x$cpu"
done
expect smp4 '^smp: 4 cpus online$' "four CPUs online"
expect smp4 "$selftest_ok" "SMP self-test: SGI call to each CPU, its own struct cpu, remap seen by all, lock and atomic counts"
for cpu in 0 1 2 3; do
    busy=$(sed -nE "s/^CPUBUSY cpu $cpu busy ([0-9]+) idle ([0-9]+) .*/\1/p" "$tmp/smp4.log.txt" | head -n 1)
    [ "${busy:-0}" -gt 0 ]
    result $? "smp4: cpu $cpu counted ${busy:-no} busy ticks of its own timer"
done
expect smp4 '^CPUBUSY 4 cpus, children together ran on 0xf$' "eight CPU-bound children ran on all four CPUs"
expect smp4 '^CPUBUSY OK$' "every CPU busy while eight tasks wait"
no_crash smp4

# ---- secondaries entered at EL2 --------------------------------------
boot el2 ",virtualization=on" "-smp 4" "samos.run=HELLO.ELF" "$done_re"
expect el2 '^SamOS aarch64: booting \(entered at EL2, running at EL1\)$' "boot CPU entered at EL2"
expect el2 '^smp: 4 cpus online$' "three secondaries through PSCI over smc, from EL2"
expect el2 "$selftest_ok" "SMP self-test"
no_crash el2

# ---- CPUs that do not start ----------------------------------------------
qemu-system-aarch64 -machine virt,gic-version=3,dumpdtb="$tmp/virt.dtb" -cpu cortex-a72 -m 512M -smp 4 \
    -nographic > /dev/null 2>&1
python3 -I "$here/dtb-cpu.py" "$tmp/virt.dtb" "$tmp/nogicr.dtb" 0x7 &&
    python3 -I "$here/dtb-cpu.py" "$tmp/virt.dtb" "$tmp/dup.dtb" 0x1
result $? "device trees with an extra CPU node (dtb-cpu.py)"

boot nogicr "" "-smp 4 -dtb $tmp/nogicr.dtb" "samos.run=HELLO.ELF" "$done_re"
expect nogicr '^smp: mpidr 0x7: no GIC redistributor, not started$' "MPIDR 0x7 reported and skipped"
for cpu in 1 2 3; do
    expect nogicr "^smp: cpu $cpu mpidr 0x$cpu online\$" "CPU $cpu still in slot $cpu"
done
expect nogicr '^smp: 4 cpus online$' "four CPUs online"
expect nogicr "$selftest_ok" "SMP self-test"
no_crash nogicr

boot dup "" "-smp 4 -dtb $tmp/dup.dtb" "samos.run=HELLO.ELF" "$done_re"
expect dup '^smp: mpidr 0x1: PSCI CPU_ON returned -4$' "CPU_ON failing (ALREADY_ON) reported"
expect dup '^smp: cpu 2 mpidr 0x2 online$' "the next CPU takes the free slot"
expect dup '^smp: 4 cpus online$' "four CPUs online"
expect dup "$selftest_ok" "SMP self-test"
no_crash dup

# ---- panic stop ------------------------------------------------------
boot panic "" "-smp 4" "samos.smp_panic_test" 'KERNEL PANIC: smp panic test'
expect panic '^panic: stopped 3 other cpus$' "the panic stops the three other CPUs (FIQ)"
expect panic 'KERNEL PANIC: smp panic test' "the panic message"

echo "aarch64-smp arch test: $checks checks, $failures failures"
[ $failures -eq 0 ]
