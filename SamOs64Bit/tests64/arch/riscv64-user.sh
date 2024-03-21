#!/usr/bin/env bash
# tests64/arch/riscv64-user.sh - the shared kernel_main on riscv64
# runs U-mode programs (QEMU virt under OpenSBI). Run after
#   bash arch-build.sh riscv64
# which builds bin/riscv64/kernel.img and bin/riscv64/initrd.img, the
# FAT16 RAM disk with BLANK.ELF, HELLO.ELF, SHELL.ELF, RVTEST.ELF and
# WRONGM.ELF (RVTEST.ELF with e_machine EM_X86_64). Programs start with
# samos.run= on the command line, ahead of BLANK.ELF, whose selftest
# runs in every boot and ends with "e2e: user selftest done".
#
# Every boot runs twice: Sv48 (default CPU) and Sv39 (-cpu rv64,sv48=off).
#   1. user: HELLO.ELF prints its arguments through ecall; RVTEST.ELF
#      reads sstatus (illegal in U-mode) and loads from 0x10: each dies
#      with a USER FAULT naming scause, sepc and stval, the rest runs
#      on; WRONGM.ELF is refused, RVTEST.ELF itself loads.
#   2. regs: two RVTEST.ELF regs tasks: x1-x31, f0-f31 and fcsr survive
#      a syscall, a syscall that sleeps and switches tasks, and a loop
#      the timer preempts (the riscv64 e2e/153).
#   3. sched: two CPU-bound RVTEST.ELF spin tasks that never yield
#      interleave their lines (behaviour, not host speed: each one's
#      lines start before the other's end).
#   4. parent: waitpid gets exit(42), main's 7, 128 + scause for an
#      illegal instruction and load/store page faults, 137 for a kill;
#      bad pointers to waitpid, fopen, fstat and fread fail (-EFAULT
#      for waitpid); the wrong-machine ELF gives -EBADEXEC.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
kernel="${RISCV64_KERNEL:-$here/../../bin/riscv64/kernel.img}"
initrd="${RISCV64_INITRD:-$here/../../bin/riscv64/initrd.img}"
for f in "$kernel" "$initrd"; do
    [ -f "$f" ] || { echo "FAIL: $f missing; run bash arch-build.sh riscv64" >&2; exit 2; }
done
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
fails=0
checks=0

# boot <name> <append> <regex>...: QEMU with $cpu until every regex is
# in the output, or 180 s pass (a failure guard only; the raw output
# has \r\n, so a regex here ends without $); output in
# $tmp/<mode>-<name>.log without \r.
boot() {
    local log="$tmp/$mode-$1.log"
    qemu-system-riscv64 -machine virt -cpu "$cpu" -m 512M -nographic -bios default \
        -kernel "$kernel" -initrd "$initrd" -append "samos.test=1 $2" < /dev/null > "$log.raw" 2>&1 &
    local pid=$! left=1800 done re
    shift 2
    while [ $left -gt 0 ] && kill -0 $pid 2>/dev/null; do
        done=1
        for re in "$@"; do
            grep -aEq "$re" "$log.raw" 2>/dev/null || { done=0; break; }
        done
        [ $done -eq 1 ] && break
        left=$((left - 1))
        sleep 0.1
    done
    sleep 0.3
    kill $pid 2>/dev/null
    wait $pid 2>/dev/null
    tr -d '\r' < "$log.raw" > "$log"
    cur="$log"
}

pass() { checks=$((checks + 1)); echo "  ok: $1"; }
fail() { checks=$((checks + 1)); echo "  FAIL: $1"; fails=$((fails + 1)); }
check() { if [ "$1" -eq 0 ]; then pass "$2"; else fail "$2"; fi; }
expect() { grep -aEq "$1" "$cur"; check $? "$2"; }
expect_count() {
    local n
    n=$(grep -aEc "$1" "$cur")
    [ "$n" -eq "$2" ]
    check $? "$3 (/$1/ x$n, want $2)"
}
# '#' delimits: the patterns hold paths (@:/RVTEST.ELF).
field() { sed -nE "s#$1#\1#p" "$cur" | head -1; }
# Line number of the first / last line matching a regex (0 if none).
first() { grep -anE "$1" "$cur" | head -1 | cut -d: -f1 | grep . || echo 0; }
last() { grep -anE "$1" "$cur" | tail -1 | cut -d: -f1 | grep . || echo 0; }

common() {
    expect "^mmu: $mode, $levels levels, root" "paging in $mode"
    expect '^SamOS: kernel_main$' "the shared kernel_main"
    expect '^disk 0 real RAMDISK fs=FAT label=SAMOS +rw$' "the initrd mounted as the RAM disk"
    expect '^e2e: user selftest done$' "BLANK.ELF selftest done"
    expect_count 'KERNEL FAULT|KERNEL PANIC|REGS FAIL|PARENT FAIL|survived the fault' 0 "no kernel fault"
}

# The USER FAULT line for the fault RVTEST.ELF announced: scause, the
# probe's address as sepc, the faulting address as stval.
user_fault() {
    local kind="$1" scause="$2" name="$3" at addr sepc stval
    at=$(field "^RVTEST fault $kind.* at (0x[0-9a-f]+)$")
    addr=$(field "^RVTEST fault $kind from (0x[0-9a-f]+) at .*$")
    sepc=$(field "^USER FAULT: scause $scause \($name\) in @:/RVTEST.ELF: sepc (0x[0-9a-f]+) stval .*$")
    stval=$(field "^USER FAULT: scause $scause \($name\) in @:/RVTEST.ELF: sepc 0x[0-9a-f]+ stval (0x[0-9a-f]+)$")
    expect "^USER FAULT: scause $scause \($name\) in @:/RVTEST.ELF: " "$kind: USER FAULT, scause $scause ($name)"
    [ -n "$at" ] && [ "$at" = "$sepc" ]
    check $? "$kind: sepc is the faulting instruction (${sepc:-none} vs ${at:-none})"
    if [ "$kind" = load ]; then
        [ -n "$addr" ] && [ "$((stval))" -eq "$((addr))" ]
        check $? "$kind: stval is the address (${stval:-none} vs ${addr:-none})"
    else
        # An illegal instruction's stval is the instruction: csrr a0, sstatus.
        [ "${stval:-x}" = 0x10002573 ]
        check $? "$kind: stval is the instruction (${stval:-none})"
    fi
}

for mode in sv48 sv39; do
    if [ $mode = sv48 ]; then cpu=rv64 levels=4; else cpu=rv64,sv48=off levels=3; fi

    echo "riscv64 $mode user"
    boot user "samos.run=HELLO.ELF one two;RVTEST.ELF fault csr;RVTEST.ELF fault load;WRONGM.ELF;RVTEST.ELF" \
        '^e2e: user selftest done' '^USER FAULT: scause 0xd' '^USER FAULT: scause 0x2'
    common
    expect '^samos.run: loaded @:/HELLO.ELF$' "HELLO.ELF loaded from the RAM disk"
    expect '^hello: argc=3$' "HELLO.ELF printed through ecall"
    expect '^hello: argv\[2\]=two$' "its arguments reached U-mode"
    expect '^RVTEST hello from U-mode, pid [0-9]+$' "RVTEST.ELF ran"
    user_fault csr 0x2 'illegal instruction'
    user_fault load 0xd 'load page fault'
    expect_count '^process killed$' 2 "each faulting process killed"
    [ "$(first '^USER FAULT: scause 0x2')" -lt "$(last '^e2e: user selftest done$')" ]
    check $? "the kernel went on after the faults (BLANK.ELF finished later)"
    expect '^samos.run: failed to load @:/WRONGM.ELF$' "the EM_X86_64 ELF refused"
    expect_count '^samos.run: loaded @:/RVTEST.ELF$' 3 "the same file with EM_RISCV loaded"

    echo "riscv64 $mode regs"
    boot regs "samos.run=RVTEST.ELF regs 1;RVTEST.ELF regs 2" '^REGS (OK|FAIL) 1' '^REGS (OK|FAIL) 2'
    expect_count '^REGS FAIL' 0 "no register changed across a syscall, a task switch or a preemption"
    expect '^REGS OK 1$' "task 1: x1-x31, f0-f31, fcsr intact"
    expect '^REGS OK 2$' "task 2: x1-x31, f0-f31, fcsr intact"
    expect_count 'KERNEL FAULT|KERNEL PANIC|USER FAULT' 0 "no fault"

    echo "riscv64 $mode sched"
    boot sched "samos.run=RVTEST.ELF spin A 12;RVTEST.ELF spin B 12" '^SPIN A done' '^SPIN B done'
    expect_count '^SPIN A [0-9]+$' 12 "A printed all its lines"
    expect_count '^SPIN B [0-9]+$' 12 "B printed all its lines"
    [ "$(first '^SPIN A [0-9]')" -lt "$(last '^SPIN B [0-9]')" ] && [ "$(first '^SPIN B [0-9]')" -lt "$(last '^SPIN A [0-9]')" ]
    check $? "the two CPU-bound tasks interleave (each starts before the other ends)"
    runs=$(grep -aE '^SPIN [AB] [0-9]+$' "$cur" | cut -c6 | uniq | wc -l)
    [ "$runs" -ge 4 ]
    check $? "the timer switches between them repeatedly ($runs runs of A/B lines)"
    expect_count 'KERNEL FAULT|KERNEL PANIC|USER FAULT' 0 "no fault"

    echo "riscv64 $mode parent"
    boot parent "samos.run=RVTEST.ELF parent" '^PARENT (OK|FAIL)'
    expect '^PARENT exit\(42\) status ok 42$' "waitpid returns exit(42)"
    expect '^PARENT main returning 7 ok 7$' "waitpid returns main's 7"
    expect '^PARENT illegal instruction is 128 \+ 2 ok 130$' "a child's illegal instruction: 130"
    expect '^PARENT load page fault is 128 \+ 13 ok 141$' "a child's load page fault: 141"
    expect '^PARENT store page fault is 128 \+ 15 ok 143$' "a child's store page fault: 143"
    expect '^PARENT killed status is 128 \+ 9 ok 137$' "a killed child: 137"
    expect '^PARENT waitpid into the kernel ok -20$' "waitpid with a kernel pointer: -EFAULT"
    expect '^PARENT waitpid into an unmapped page ok -20$' "waitpid with an unmapped pointer: -EFAULT"
    expect '^PARENT fstat into the kernel ok 1$' "fstat with a kernel pointer refused"
    expect '^PARENT fread into the kernel ok 1$' "fread with a kernel pointer refused"
    expect '^PARENT wrong-machine ELF refused, -EBADEXEC ok -22$' "the wrong-machine ELF: -EBADEXEC"
    expect '^PARENT no child left to wait for ok -23$' "waitpid with no child: -ECHILD"
    expect '^PARENT OK$' "every parent check"
    expect_count 'KERNEL FAULT|KERNEL PANIC' 0 "no kernel fault"
done

# Summary line read by tests64/arch/run.sh (the arch tests' format).
echo "riscv64 arch test: $checks checks, $fails failures"
if [ $fails -eq 0 ]; then
    echo "PASS: tests64/arch/riscv64-user"
else
    echo "FAIL: tests64/arch/riscv64-user ($fails failed)"
    exit 1
fi
