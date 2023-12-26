#!/usr/bin/env bash
# tests64/arch/riscv64-hello.sh - the riscv64 kernel boots under
# OpenSBI on qemu-system-riscv64 -machine virt and says hello over the
# console. Not part of the x86 e2e suite (tests64/e2e); run it after
#   bash arch-build.sh riscv64
#
# Boots:
#   1. -smp 1: banner, hart id, DTB address, SBI info, the console
#      moved to the device tree's 16550, 512 MiB, one hart, bootargs,
#      hello, then samos.test=1 powers QEMU off through SBI SRST.
#   2. -smp 4: one banner only; the device tree lists four harts and
#      HSM reports the boot hart started and three stopped, so only
#      one hart runs the kernel.
#   3. samos.trap_test: a load from OpenSBI's PMP-protected image
#      ends in the trap dump (load access fault, scause 5).
#      renamed its header from EARLY FAULT to KERNEL FAULT (x86 wording)
#      once the dump became the real trap path's, not an early stub.
#      Moved the kernel to the higher half (sepc 0xffffffff802...)
#      and the load goes through the Sv48 direct map, so stval is
#      0xffff800000000000 + 0x80000000; still a PMP access fault.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
kernel="${RISCV64_KERNEL:-$here/../../bin/riscv64/kernel.img}"
[ -f "$kernel" ] || { echo "FAIL: $kernel missing; run bash arch-build.sh riscv64" >&2; exit 2; }
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
fails=0
checks=0

# boot <name> <smp> <append>: run QEMU until it powers off (or the
# kernel halts, or 20 s pass); output in $tmp/<name>.log without the
# \r of \r\n.
boot() {
    local log="$tmp/$1.log"
    qemu-system-riscv64 -machine virt -m 512M -smp "$2" -nographic -bios default \
        -kernel "$kernel" -append "$3" < /dev/null > "$log.raw" 2>&1 &
    local pid=$! left=200
    powered_off=0
    while [ $left -gt 0 ]; do
        kill -0 $pid 2>/dev/null || { powered_off=1; break; }
        # Halted: give the SBI shutdown half a second, no more.
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

expect() {
    checks=$((checks + 1))
    if grep -Eq "$1" "$cur"; then
        echo "  ok: $2"
    else
        echo "  FAIL: $2 (no match for /$1/ in $(basename "$cur"))"
        fails=$((fails + 1))
    fi
}

expect_count() {
    local n
    checks=$((checks + 1))
    n=$(grep -Ec "$1" "$cur")
    if [ "$n" -eq "$2" ]; then
        echo "  ok: $3"
    else
        echo "  FAIL: $3 (/$1/ matched $n times, want $2)"
        fails=$((fails + 1))
    fi
}

echo "riscv64 -smp 1"
boot smp1 1 "samos.test=1"
expect '^SamOS riscv64 \(S-R[0-9]+\)$' "banner"
expect '^boot hart: 0$' "boot hart id"
expect '^dtb: 0x[0-9a-f]+$' "device tree address"
expect '^sbi: extensions .*hsm' "SBI extensions probed"
expect '^console: ns16550a at 0x10000000$' "console moved to the device tree's 16550"
expect '^memory: 0x80000000 size 0x20000000$' "memory node"
expect '^memory: 512 MiB in 1 region\(s\)$' "memory size"
expect '^harts: 1$' "hart count from /cpus"
expect '^bootargs: samos.test=1$' "bootargs"
expect '^hsm: 0=started$' "one hart, started"
expect '^SamOS riscv64: hello$' "hello"
expect '^riscv64: halted$' "halted"
expect_count 'FAULT:' 0 "no trap"
checks=$((checks + 1))
if [ $powered_off -eq 1 ]; then echo "  ok: QEMU powered off by SBI"; else echo "  FAIL: QEMU still running"; fails=$((fails + 1)); fi

echo "riscv64 -smp 4"
boot smp4 4 "samos.test=1"
expect_count '^SamOS riscv64 \(S-R[0-9]+\)$' 1 "one banner"
expect '^harts: 4$' "four harts in /cpus"
expect '^hsm: ([0-3]=(started|stopped) ?){4}$' "four harts listed"
expect '^hsm: ([0-3]=stopped )*[0-3]=started( [0-3]=stopped)*$' "only the boot hart started, three stopped"
expect '^SamOS riscv64: hello$' "hello"

echo "riscv64 samos.trap_test"
boot trap 1 "samos.test=1 samos.trap_test"
expect '^bootargs: samos.test=1 samos.trap_test$' "bootargs"
expect '^KERNEL FAULT: scause 0x5 \(load access fault\)$' "scause dump"
expect '^  sepc 0xffffffff802[0-9a-f]{5} stval 0xffff800080000000 sstatus 0x[0-9a-f]+$' "sepc in the kernel, stval the address"
expect '^  ra 0x[0-9a-f]+ sp 0x[0-9a-f]+ gp 0x[0-9a-f]+ tp 0x[0-9a-f]+$' "register dump"
expect '^System halted\.$' "halted after the dump"
expect_count '^SamOS riscv64: hello$' 0 "no hello after the trap"

# Summary line read by tests64/arch/run.sh and run-all.sh (the arch tests' format).
echo "riscv64 arch test: $checks checks, $fails failures"
if [ $fails -eq 0 ]; then
    echo "PASS: tests64/arch/riscv64-hello"
else
    echo "FAIL: tests64/arch/riscv64-hello ($fails failed)"
    exit 1
fi
