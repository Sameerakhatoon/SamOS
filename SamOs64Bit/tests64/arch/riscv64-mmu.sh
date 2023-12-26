#!/usr/bin/env bash
# tests64/arch/riscv64-mmu.sh - Sv48 and Sv39 paging on riscv64
# (QEMU virt under OpenSBI). Run after
#   bash arch-build.sh riscv64
#
# Everything runs twice: on the default CPU, where the satp probe
# accepts Sv48, and on -cpu rv64,sv48=off, where it falls back to Sv39
# (the plan's "both modes are tested"). Per mode:
#   1. samos.mmu_test at -m 128M, 512M and 2G: the probe result and the
#      mode's layout; the identity map gone; text r-x, rodata r--,
#      data/bss/stack rw-, the UART rw- (never x), no W+X and no U leaf
#      in the kernel tables; the frame allocator from /memory minus
#      OpenSBI's /reserved-memory, the image, the boot tables and stack
#      and the DTB; every free frame allocated, checked and freed; the
#      uaccess tests against a user address space (read, write, a
#      read-only page, a fault mid-copy, kernel pointers refused,
#      strncpy_from_user, SUM clear outside the copies and in trap
#      handlers, two address spaces on ASIDs).
#   2. samos.mmu_noasid: the same address-space tests with a full
#      flush on every satp switch instead of ASIDs.
#   3. samos.mmu_fault=wtext|wrodata|xdata|ident: a store to kernel
#      text and to rodata (store page fault), a jump into data
#      (instruction page fault), a load through the removed identity
#      map (load page fault); scause, stval and sepc checked.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
kernel="${RISCV64_KERNEL:-$here/../../bin/riscv64/kernel.img}"
[ -f "$kernel" ] || { echo "FAIL: $kernel missing; run bash arch-build.sh riscv64" >&2; exit 2; }
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
fails=0
checks=0

# boot <name> <mem> <append>: QEMU with $cpu until it powers off (or
# the kernel halts, or 60 s pass, a failure guard only); output in
# $tmp/<name>.log without the \r of \r\n.
boot() {
    local log="$tmp/$1.log"
    qemu-system-riscv64 -machine virt -cpu "$cpu" -m "$2" -nographic -bios default \
        -kernel "$kernel" -append "$3" < /dev/null > "$log.raw" 2>&1 &
    local pid=$! left=600
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
check() { if [ "$1" -eq 0 ]; then pass "$2"; else fail "$2"; fi; }
expect() { grep -Eq "$1" "$cur"; check $? "$2"; }
expect_count() {
    local n
    n=$(grep -Ec "$1" "$cur")
    [ "$n" -eq "$2" ]
    check $? "$3 (/$1/ x$n, want $2)"
}
field() { sed -nE "s/$1/\1/p" "$cur" | head -1; }

# The probe, the layout and the kernel's own mappings (every boot).
check_layout() {
    expect "^mmu: satp probe: $probe" "satp probe: $mode"
    expect "^mmu: $mode, $levels levels, root 0x[0-9a-f]+$" "tables for $mode, $levels levels"
    expect "^mmu: kernel 0xffffffff80200000 at 0x80200000, direct map $dmap, mmio $mmio, user below $uend$" "higher-half layout for $mode"
    expect '^mmu: identity map removed, 0 low-half root entries$' "identity map removed"
    expect '^mmu: text r-x-g rodata r---g data rw--g bss rw--g stack rw--g$' "W^X: text r-x, rodata r--, data/bss/stack rw-"
    expect "^mmu: uart ${mmio} rw--g$" "UART in the mmio window, rw, not executable"
    expect "^mmu: direct map $(printf '0x%x' $((dmap + 0x80200000))) rw--g$" "direct map rw, not executable"
    expect '^mmu: 0 writable\+executable leaves, 0 user leaves in the kernel tables$' "no W+X leaf, no U leaf in the kernel tables"
    expect '^SamOS riscv64: hello$' "hello"
    expect_count 'FAULT:|unexpected|no handler' 0 "no fault"
}

# The frame allocator at -m $1 MiB.
check_pfa() {
    local mib="$1" dtb frames free n
    expect "^pfa: reserve 0x80000000-0x[0-9a-f]+ mmode_resv" "OpenSBI's /reserved-memory at the RAM base reserved"
    expect '^pfa: reserve 0x80200000-0x[0-9a-f]+ kernel image$' "kernel image reserved"
    expect '^pfa: reserve 0x[0-9a-f]+-0x[0-9a-f]+ boot page tables$' "boot page tables reserved"
    expect '^pfa: reserve 0x[0-9a-f]+-0x[0-9a-f]+ boot stack$' "boot stack reserved"
    dtb=$(field '^dtb: (0x[0-9a-f]+)$')
    expect "^pfa: reserve ${dtb:-none}-0x[0-9a-f]+ device tree$" "device tree reserved (${dtb:-none})"
    read -r frames free <<< "$(sed -nE 's/^pfa: ([0-9]+) frames \([0-9]+ MiB\): ([0-9]+) free, [0-9]+ reserved$/\1 \2/p' "$cur" | head -1)"
    [ "${frames:-0}" -eq $((mib * 256)) ] && [ "${free:-0}" -gt 0 ]
    check $? "$((mib * 256)) frames for $mib MiB (${frames:-?}, ${free:-?} free)"
    n=$(field '^pfa test: ([0-9]+) frames allocated .*$')
    expect "^pfa test: ${n:-x} frames allocated \(${n:-x} were free\), 0 outside memory or reserved$" "every free frame allocated, none in a reservation (${n:-?})"
    expect "^pfa test: ${n:-x} read back, ${n:-x} freed, ${n:-x} free now$" "each written through the direct map, read back and freed"
    expect "^pfa test: run of 64 at 0x[0-9a-f]+ ok, free 0, again -1, ${n:-x} free now$" "contiguous run; a double free is refused"
}

check_uaccess() {
    local asids="$1" what="$2"
    expect "^uaccess: a: 0x10000 rw, 0x11000 r, 0x12000 unmapped; b: 0x10000 rw; $asids" "two user address spaces, $what"
    expect "^uaccess: read 0 'user a'$" "copy_from_user reads a user page"
    expect "^uaccess: write 0, page holds 'written by the kernel'$" "copy_to_user writes a user page"
    expect "^uaccess: write to the read-only page -20, 1 fault\(s\), scause 0xf, page holds 'r'$" "write to a read-only user page: -EFAULT after a store page fault"
    expect "^uaccess: read across into the unmapped page -20 after 2048 bytes, 1 fault\(s\), scause 0xd$" "page fault mid-copy: -EFAULT, the bytes before it copied"
    expect "^uaccess: kernel image -20, direct map -20, past user_end -20, wrapping -20, 0 fault\(s\)$" "kernel pointers and bad ranges refused without an access"
    expect "^uaccess: strncpy 5 'hello'$" "strncpy_from_user"
    expect "^uaccess: strncpy into 4 bytes -21 'hel'$" "strncpy_from_user: -ENAMETOOLONG, truncated and terminated"
    expect "^uaccess: strncpy into the unmapped page -20, from the kernel -20$" "strncpy_from_user: faults and kernel pointers"
    expect "^uaccess: sstatus.SUM after the copies 0$" "SUM clear after the copies"
    expect "^uaccess: user page without SUM: 1 byte\(s\) left, 1 fault\(s\), scause 0xd$" "without SUM the kernel cannot read a U page"
    expect "^uaccess: ticks during SUM: taken, 0 handler\(s\) ran with SUM, SUM after sret 1$" "trap handlers run with SUM clear, sret restores it"
    expect "^uaccess: mm b reads 0 'user b', mm a again 0 'user a'$" "satp switch between address spaces"
    expect "^uaccess: kernel root -20$" "the kernel root has no user pages"
}

# fault_boot <kind> <scause regex> <test line regex>: the line names
# the address (stval) and the faulting instruction (sepc).
fault_boot() {
    boot "$mode-$1" 128M "samos.test=1 samos.mmu_fault=$1"
    local addr at stval sepc
    addr=$(field "^mmu fault test: $3 (0x[0-9a-f]+) at 0x[0-9a-f]+$")
    at=$(field "^mmu fault test: $3 0x[0-9a-f]+ at (0x[0-9a-f]+)$")
    sepc=$(field '^  sepc (0x[0-9a-f]+) stval .*$')
    stval=$(field '^  sepc 0x[0-9a-f]+ stval (0x[0-9a-f]+) .*$')
    expect "^KERNEL FAULT: scause $2$" "$1: scause $2"
    [ -n "$addr" ] && [ "$addr" = "$stval" ]
    check $? "$1: stval is the address (${stval:-none} vs ${addr:-none})"
    [ -n "$at" ] && [ "$at" = "$sepc" ]
    check $? "$1: sepc is the faulting instruction (${sepc:-none} vs ${at:-none})"
    expect '^System halted\.$' "$1: fatal"
    expect_count 'mmu fault test: no fault' 0 "$1: did not go on"
}

for mode in sv48 sv39; do
    if [ $mode = sv48 ]; then
        cpu=rv64 levels=4 probe='sv48 accepted \(read back 0x9[0-9a-f]+\)$'
        dmap=0xffff800000000000 mmio=0xffffc00000000000 uend=0x800000000000
    else
        cpu=rv64,sv48=off levels=3 probe='sv48 rejected, sv39 \(read back 0x0\)$'
        dmap=0xffffffc000000000 mmio=0xffffffe000000000 uend=0x4000000000
    fi
    for mib in 128 512 2048; do
        echo "riscv64 $mode -cpu $cpu -m ${mib}M samos.mmu_test"
        boot "$mode-$mib" "${mib}M" "samos.test=1 samos.mmu_test"
        check_layout
        check_pfa $mib
        check_uaccess 'asid a 1 b 2 \(16 bits\)$' "ASIDs 1 and 2"
        [ $powered_off -eq 1 ]
        check $? "QEMU powered off"
    done

    echo "riscv64 $mode samos.mmu_noasid"
    boot "$mode-noasid" 512M "samos.test=1 samos.mmu_test samos.mmu_noasid"
    expect '^mmu: asids off$' "ASIDs off"
    check_uaccess 'asid a 0 b 0 \(0 bits\)$' "no ASIDs, flush on switch"

    echo "riscv64 $mode faults"
    fault_boot wtext '0xf \(store page fault\)' 'store to text'
    fault_boot wrodata '0xf \(store page fault\)' 'store to rodata'
    fault_boot xdata '0xc \(instruction page fault\)' 'execute data'
    fault_boot ident '0xd \(load page fault\)' 'load from the old identity map'
done

# Summary line read by tests64/arch/run.sh (the arch tests' format).
echo "riscv64 arch test: $checks checks, $fails failures"
if [ $fails -eq 0 ]; then
    echo "PASS: tests64/arch/riscv64-mmu"
else
    echo "FAIL: tests64/arch/riscv64-mmu ($fails failed)"
    exit 1
fi
