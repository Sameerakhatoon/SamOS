#!/usr/bin/env bash
# tests64/arch/riscv64-traps.sh - traps, the 100 Hz timer, the
# PLIC, and timer preemption of two kernel threads on riscv64 (QEMU
# virt under OpenSBI). Run after
#   bash arch-build.sh riscv64
#
# Boots:
#   1. default CPU (Sstc on): timer on stimecmp, PLIC context for the
#      boot hart, UART irq from the device tree; threads a and b busy
#      loop and interleave, numbered without gaps; ticks agree with
#      the time CSR.
#   2. -cpu rv64,sstc=off: the same, with the timer on SBI set_timer.
#   3. -smp 4: the PLIC context follows the boot hart (2 * hart + 1 on
#      QEMU), threads and ticks as in 1, the other harts stay stopped.
#   4. samos.uart_test with a line typed into the serial port: it
#      arrives through the PLIC as the UART's receive interrupt.
#   5-7. samos.trap_test=illegal|load|ebreak: the dump's scause and
#      sepc match the faulting instruction the kernel names; the
#      breakpoint is stepped over and boot goes on.
# samos.bringup on every boot: these are the riscv64 trap boot (kthreads
# on the timer), not the shared kernel_main (riscv64-user.sh).
# All seven run twice, with the MMU on in Sv48 (default CPU)
# and in Sv39 (-cpu rv64,sv48=off); the load test goes through the
# direct map.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
kernel="${RISCV64_KERNEL:-$here/../../bin/riscv64/kernel.img}"
[ -f "$kernel" ] || { echo "FAIL: $kernel missing; run bash arch-build.sh riscv64" >&2; exit 2; }
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
fails=0
checks=0

# boot <name> <smp> <append> [qemu args...]: run QEMU until it powers
# off (or the kernel halts, or 30 s pass); output in $tmp/<name>.log
# without the \r of \r\n. Serial input comes from $INPUT if set: one
# line, typed once the kernel prints "uart test: waiting" (by then the
# UART is set up and its interrupt enabled; bytes typed before
# uart16550_init would be lost to its FIFO reset). Keyed to
# the kernel's output, not a delay, so a slow host only takes longer.
boot() {
    local name="$1" smp="$2" append="$3 samos.bringup"
    shift 3
    local log="$tmp/$mode-$name.log" cpu="rv64${CPUOPT:-}$mode_opt"
    if [ -n "${INPUT:-}" ]; then
        { for _ in $(seq 300); do grep -q 'uart test: waiting' "$log.raw" 2>/dev/null && break; sleep 0.1; done
          printf '%s\n' "$INPUT"; sleep 30; } | qemu-system-riscv64 -machine virt -m 512M \
            -smp "$smp" -nographic -bios default -cpu "$cpu" "$@" -kernel "$kernel" -append "$append" > "$log.raw" 2>&1 &
    else
        qemu-system-riscv64 -machine virt -m 512M -smp "$smp" -nographic -bios default -cpu "$cpu" "$@" \
            -kernel "$kernel" -append "$append" < /dev/null > "$log.raw" 2>&1 &
    fi
    local pid=$! left=300
    powered_off=0
    while [ $left -gt 0 ]; do
        kill -0 $pid 2>/dev/null || { powered_off=1; break; }
        if [ $left -gt 5 ] && grep -q 'riscv64: halted' "$log.raw" 2>/dev/null; then
            left=5
        fi
        left=$((left - 1))
        sleep 0.1
    done
    kill $pid 2>/dev/null
    wait $pid 2>/dev/null
    tr -d '\r' < "$log.raw" > "$log"
    cur="$log"
}

pass() { checks=$((checks + 1)); echo "  ok: $1"; }
fail() { checks=$((checks + 1)); echo "  FAIL: $1"; fails=$((fails + 1)); }
# check <condition-status> <description>
check() { if [ "$1" -eq 0 ]; then pass "$2"; else fail "$2"; fi; }

expect() {
    grep -Eq "$1" "$cur"
    check $? "$2"
}

expect_count() {
    local n
    n=$(grep -Ec "$1" "$cur")
    [ "$n" -eq "$2" ]
    check $? "$3 (/$1/ x$n, want $2)"
}

# First capture group of the first line matching sed regex $1.
field() { sed -nE "s/$1/\1/p" "$cur" | head -1; }

# Threads a and b. Every check is on behaviour, not host speed (as in
# the scheduler tests): a loaded host delays ticks, and the late
# ones then arrive back to back (deadlines are absolute) and give a
# thread slices too short to print in, so line counts depend on the
# host; what the scheduler did does not.
#   - both printed, the output alternates a/b at least twice (runs =
#     number of a/b changes + 1), each counts 0, 1, 2... with no gap
#     or repeat (a thread's registers survive every switch);
#   - the scheduler's own slice counts: every tick switches between
#     the two ready threads, so they alternate strictly (counts differ
#     by at most 1) over the 50 ticks (at least 20 each);
#   - idle was switched to exactly once, when both had ended (not
#     "idle woke from wfi": the last tick can come before idle reaches
#     its wfi, which a slow host makes likely);
#   - ticks agree with the time CSR within 2 (a late tick is made up
#     at once, so the count lags the counter by at most one).
check_sched() {
    local stats a b runs bad
    stats=$(awk '/^thread [ab]: [0-9]+$/ {
            split($0, p, /[ :]+/); name = p[2]; n = p[3] + 0
            if (name != last) runs++
            last = name
            if (n != want[name] + 0) bad++
            want[name] = n + 1; cnt[name]++
        } END { printf "%d %d %d %d\n", cnt["a"], cnt["b"], runs, bad }' "$cur")
    read -r a b runs bad <<< "$stats"
    [ "$a" -ge 1 ] && [ "$b" -ge 1 ]
    check $? "both threads printed (a $a lines, b $b lines)"
    [ "$runs" -ge 3 ]
    check $? "output interleaves ($runs runs of a/b)"
    [ "$bad" -eq 0 ]
    check $? "each thread counts without gaps ($bad bad)"
    local sa sb si
    read -r sa sb si <<< "$(sed -nE 's/^sched: slices a ([0-9]+) b ([0-9]+) idle ([0-9]+)$/\1 \2 \3/p' "$cur" | head -1)"
    [ -n "${si:-}" ] && [ "$sa" -ge 20 ] && [ "$sb" -ge 20 ] && [ $((sa - sb)) -le 1 ] && [ $((sb - sa)) -le 1 ]
    check $? "the timer alternates a and b (slices a ${sa:-?} b ${sb:-?})"
    [ "${si:-0}" -eq 1 ]
    check $? "idle switched to once, after both threads ended (${si:-no} slices)"
    local n d i
    read -r n d i <<< "$(sed -nE 's/^ticks: ([0-9]+) in ([0-9]+) counter ticks, interval ([0-9]+)$/\1 \2 \3/p' "$cur" | head -1)"
    if [ -n "${i:-}" ] && [ "$i" -gt 0 ]; then
        local want=$((d / i))
        [ "$n" -ge $((want - 2)) ] && [ "$n" -le $((want + 2)) ] && [ "$n" -ge 50 ]
        check $? "ticks agree with rdtime ($n ticks, $d / $i = $want)"
    else
        fail "ticks line"
    fi
}

# Every boot below runs with the MMU on, once per paging mode:
# the default CPU (Sv48) and -cpu rv64,sv48=off (Sv39).
run_mode() {
    echo "riscv64 $mode timer on sstc"
    boot sstc 1 "samos.test=1"
    expect '^timer: device tree lists sstc, stimecmp works$' "Sstc in riscv,isa and stimecmp readable"
    expect '^timer: sstc stimecmp, 100 Hz, timebase 10000000 Hz, interval 100000$' "timer on stimecmp, 100 Hz from timebase-frequency"
    expect '^plic: 0xc000000, 95 sources, context 1$' "PLIC from the device tree, hart 0 S-mode context"
    expect '^uart: rx interrupt on plic irq 10$' "UART irq from the device tree"
    expect '^interrupts: on$' "interrupts on"
    expect "^mmu: $mode, " "MMU on in $mode"
    check_sched
    expect_count 'FAULT:|unexpected|no handler' 0 "no fault or stray interrupt"
    expect '^SamOS riscv64: hello$' "hello"
    [ $powered_off -eq 1 ]
    check $? "QEMU powered off"

    echo "riscv64 $mode timer on sbi (-cpu rv64,sstc=off$mode_opt)"
    CPUOPT=,sstc=off boot sbi 1 "samos.test=1"
    expect '^timer: sbi set_timer, 100 Hz, timebase 10000000 Hz, interval 100000$' "no Sstc: SBI set_timer"
    expect '^timer: device tree does not list sstc, stimecmp traps$' "no Sstc in riscv,isa; the stimecmp probe traps and is skipped"
    check_sched
    expect_count 'FAULT:|unexpected|no handler' 0 "no fault or stray interrupt"
    expect '^SamOS riscv64: hello$' "hello"

    echo "riscv64 $mode -smp 4"
    boot smp4 4 "samos.test=1"
    hart=$(field '^boot hart: ([0-9]+)$')
    expect "^plic: 0xc000000, 95 sources, context $((2 * ${hart:-0} + 1))$" "PLIC context of boot hart ${hart:-?} (S-mode)"
    expect "^hsm: .*${hart:-x}=started" "boot hart started"
    expect_count '=started' 1 "one hart started, the rest stopped"
    check_sched
    expect '^SamOS riscv64: hello$' "hello"

    echo "riscv64 $mode uart receive interrupt"
    INPUT="samos-rx" boot uart 1 "samos.test=1 samos.uart_test"
    expect '^uart test: waiting for a line$' "waiting"
    expect "^uart rx: 'samos-rx' after [1-9][0-9]* interrupt\(s\) on irq 10$" "typed line arrived by interrupt"
    expect '^SamOS riscv64: hello$' "hello"

    # trap_test <name> <bootarg> <cause regex> <test line regex>
    trap_boot() {
        boot "$1" 1 "samos.test=1 samos.trap_test=$2"
        local at sepc
        at=$(field "^trap test: $4 at (0x[0-9a-f]+)$")
        sepc=$(field '^  sepc (0x[0-9a-f]+) stval .*$')
        expect "^(KERNEL FAULT|BREAKPOINT): scause $3$" "dump names the cause"
        [ -n "$at" ] && [ "$at" = "$sepc" ]
        check $? "sepc is the faulting instruction (${sepc:-none} vs ${at:-none})"
        expect '^  ra 0x[0-9a-f]+ sp 0x[0-9a-f]+ gp 0x[0-9a-f]+ tp 0x[0-9a-f]+$' "register dump"
    }

    echo "riscv64 $mode trap test: illegal instruction"
    trap_boot illegal illegal '0x2 \(illegal instruction\)' 'illegal instruction'
    expect '^System halted\.$' "fatal: halted"
    expect_count '^SamOS riscv64: hello$' 0 "no hello"

    echo "riscv64 $mode trap test: load access fault"
    trap_boot load load '0x5 \(load access fault\)' 'load from 0x[0-9a-f]+ \(pa 0x80000000\)'
    # Through the direct map, still a PMP access fault.
    va=$(field '^trap test: load from (0x[0-9a-f]+) \(pa 0x80000000\) at .*$')
    expect "^  sepc 0x[0-9a-f]+ stval ${va:-none} " "stval is the direct-map address of 0x80000000 (${va:-none})"
    expect '^System halted\.$' "fatal: halted"
    expect_count '^SamOS riscv64: hello$' 0 "no hello"

    echo "riscv64 $mode trap test: ebreak"
    trap_boot ebreak ebreak '0x3 \(breakpoint\)' 'ebreak'
    sepc=$(field '^  sepc (0x[0-9a-f]+) stval .*$')
    resume=$(field '^breakpoint: resuming at (0x[0-9a-f]+)$')
    len=$(( ${resume:-0} - ${sepc:-0} ))
    [ -n "$resume" ] && { [ $len -eq 2 ] || [ $len -eq 4 ]; }
    check $? "resumes past the ebreak ($len bytes)"
    expect '^trap test: ebreak returned$' "sret back into the caller"
    expect_count '^System halted\.$' 0 "not fatal"
    check_sched
    expect '^SamOS riscv64: hello$' "boot went on to hello"
}

for mode in sv48 sv39; do
    mode_opt=""
    [ $mode = sv39 ] && mode_opt=",sv48=off"
    run_mode
done

# Summary line read by tests64/arch/run.sh (the arch tests' format).
echo "riscv64 arch test: $checks checks, $fails failures"
if [ $fails -eq 0 ]; then
    echo "PASS: tests64/arch/riscv64-traps"
else
    echo "FAIL: tests64/arch/riscv64-traps ($fails failed)"
    exit 1
fi
