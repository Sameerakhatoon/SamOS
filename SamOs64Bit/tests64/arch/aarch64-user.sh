#!/usr/bin/env bash
# aarch64 user mode and syscalls: the shared kernel_main with
# EL0 tasks, svc syscalls, preemption between user tasks, the context
# switch (x0-x30, sp, ELR/SPSR, q0-q31, FPSR/FPCR), user faults,
# copy_from_user and friends, the ELF loader's machine check, waitpid.
#
# Builds bin/aarch64/kernel.img and bin/aarch64/ramdisk.img (the user
# programs on FAT16, label SAMOS) and boots them on -machine
# virt,gic-version=3 -cpu cortex-a72 -m 512M with -initrd, the RAM disk
# kernel_main finds its programs on ("@:/"). Command lines go in with
# -append, as CMDLINE.TXT does on x86 (samos.run=PROG args;PROG2 ...).
# A program's line may follow BLANK.ELF's samos_putchar('!') probe on
# the same line, so their patterns start with (^|!).
#   user:   kernel_main's boot over the DT memory map (shared PFA,
#           memtests), BLANK.ELF's selftest at EL0 printing through
#           svc, HELLO.ELF with arguments, an MSR at EL0 trapping (EL0
#           proven by the CPU, not by the kernel's word).
#   regs:   two KTEST.ELF regs tasks (x0-x30 and q0-q31 with seed
#           values, a syscall that returns at once and one that sleeps
#           and switches tasks, 20 rounds each) plus BLANK.ELF: no
#           register changes (the x86 analogue is e2e/153).
#   spin:   two CPU-bound KTEST.ELF spin tasks that never yield: their
#           lines interleave, so the tick preempts EL0 (behaviour only,
#           no timing).
#   faults: an undefined instruction, a store to an unmapped page, a
#           jump into the kernel: each kills only its process, with
#           ESR, ELR and FAR on the console; the kernel goes on.
#   kptr:   kernel and bad pointers to syscalls come back -EFAULT (-20)
#           or -ENAMETOOLONG; a string and a buffer running into an
#           unmapped page stop there (the __ex_table fixups of
#           uaccess.S).
#   parent: waitpid returns each child's status: exit(42), main's 7,
#           128 + 6 and 128 + 14 for faults, 128 + 9 for kill.
#   badelf: an x86-64 ELF and an aarch64 ELF patched to EM_RISCV are
#           refused by syscall 7; the unpatched one runs.
#   pan:    -cpu max (ARMv8.1+): PAN on, and the uaccess and register
#           tests still pass with it.
#   markers: the shared kernel_selftest, memtest and BLANK.ELF feature
#           markers, dumped with pmemsave: all pass but the device ones.
#   stack:  samos.double_fault_test: a fault whose frame cannot be
#           pushed is reported from a fresh stack.
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
timeout_s="${ARCH_TIMEOUT:-90}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

checks=0
failures=0

bash "$root/arch-build.sh" aarch64 > "$tmp/build.log" 2>&1 || {
    cat "$tmp/build.log"
    echo "FAIL: aarch64 build"
    echo "aarch64 arch test: 1 checks, 1 failures"
    exit 1
}
image="$root/bin/aarch64/ramdisk.img"

# boot <name> <cpu> <initrd> <append> <ERE>... : until every ERE matched
boot() {
    local name="$1" cpu="$2" initrd="$3" append="$4"
    shift 4
    local log="$tmp/$name.log"
    qemu-system-aarch64 -machine virt,gic-version=3 -cpu "$cpu" -m 512M -nographic \
        -kernel "$root/bin/aarch64/kernel.img" -initrd "$initrd" -append "$append" \
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

# expect_count <name> <ERE> <n> <description>
expect_count() {
    local n
    n="$(grep -acE "$2" "$tmp/$1.log.txt")"
    [ "$n" -eq "$3" ]
    result $? "$1: $4 ($n of $3)"
}

# expect_before <name> <ERE a> <ERE b> <description>: a line matching b
# comes after the first line matching a.
expect_before() {
    local a b
    a="$(grep -anE "$2" "$tmp/$1.log.txt" | head -1 | cut -d: -f1)"
    b="$(grep -anE "$3" "$tmp/$1.log.txt" | tail -1 | cut -d: -f1)"
    [ -n "$a" ] && [ -n "$b" ] && [ "$a" -lt "$b" ]
    result $? "$1: $4"
}

dump() {
    echo "---- $1 serial ----"
    cat "$tmp/$1.log.txt"
}

no_crash() {
    expect_no "$1" '^exception:|KERNEL PANIC|^trap:|^irq: unexpected' "no kernel exception, panic or stray IRQ"
}

# ---- kernel_main, EL0, svc -----------------------------------------
boot user cortex-a72 "$image" "samos.run=HELLO.ELF one two;KTEST.ELF gp" \
    '^e2e: user selftest done' '(^|!)hello: argv\[2\]=two' 'USER FAULT: system register'
dump user
expect user '^SamOS aarch64: hello$' "the early boot first"
expect user '^boot: kernel_main, [0-9]+ memory map entries, dtb 0x[0-9a-f]+, boot markers at pa 0x[0-9a-f]+$' "boot_info and the memory map built"
expect user '^SamOS: kernel_main$' "the shared kernel_main"
expect user '^boot_info: cmdline "samos.run=HELLO.ELF one two;KTEST.ELF gp"$' "bootargs as the command line"
expect user '^cpu: no PAN \(ARMv8.0\)' "Cortex-A72: no PAN, said so"
expect user '^pfa: [0-9]{3} MiB free of 511 MiB span$' "the shared PFA from the DT memory map (E820 entries)"
expect user '^memtest pfa: ok$' "memtest pfa (reservations, read-only image alias)"
expect user '^memtest steady: ok$' "memtest steady (user page tables built and freed)"
expect user '^memtest process: ok$' "memtest process (create/exit leaks nothing)"
expect user '^initrd: 0x[0-9a-f]+, 8192 KiB, the RAM disk$' "initrd from /chosen"
expect user '^disk 0 real RAMDISK fs=FAT label=SAMOS ' "FAT16 RAM disk mounted"
expect user '^samos.run: loaded @:/HELLO.ELF$' "ELF64 EM_AARCH64 loaded from the VFS"
expect user '^e2e: running user-side BLANK.ELF selftest$' "BLANK.ELF started"
expect user '^e2e: printf 7 ok$' "BLANK.ELF printf through svc"
expect user '^e2e: hello$' "BLANK.ELF print through svc"
expect user '^e2e: user selftest done$' "BLANK.ELF selftest done (file, heap, sleep syscalls)"
expect user '(^|!)hello: argc=3$' "HELLO.ELF argc"
expect user '(^|!)hello: argv\[1\]=one$' "HELLO.ELF argv"
expect user '(^|!)KTEST gp: masking IRQs at EL0$' "KTEST gp ran"
expect user '^USER FAULT: system register access in @:/KTEST.ELF: ESR 0x62[0-9a-f]{6} ELR 0x4[0-9a-f]{5} FAR 0x[0-9a-f]+$' "MSR DAIFSet at EL0 traps (EC 0x18): user code runs at EL0"
no_crash user

# ---- registers across a syscall and a task switch -------------------
boot regs cortex-a72 "$image" "samos.run=KTEST.ELF regs 1;KTEST.ELF regs 2" \
    '(^|!)REGS (OK|FAIL) 1' '(^|!)REGS (OK|FAIL) 2' '^e2e: user selftest done'
dump regs
expect regs '(^|!)REGS OK 1$' "task 1: x0-x30, q0-q31 intact over sum and udelay, 20 rounds"
expect regs '(^|!)REGS OK 2$' "task 2: x0-x30, q0-q31 intact over sum and udelay, 20 rounds"
expect_no regs 'REGS FAIL' "no register changed"
expect regs '^e2e: user selftest done$' "BLANK.ELF ran alongside"
no_crash regs

# ---- preemption between user tasks ---------------------------------
boot spin cortex-a72 "$image" "samos.run=KTEST.ELF spin A 6;KTEST.ELF spin B 6" \
    '(^|!)SPIN A done' '(^|!)SPIN B done'
dump spin
seq="$(grep -aoE '(^|!)SPIN [AB] [0-9]+$' "$tmp/spin.log.txt" | tr -d '!' | cut -c6 | tr -d '\n')"
runs="$(echo "$seq" | fold -w1 | uniq | wc -l)"
[ "${#seq}" -eq 12 ] && [ "$runs" -ge 4 ]
result $? "spin: two tasks that never yield interleave (${seq:-none}, $runs runs)"
for t in A B; do
    got="$(grep -aoE "(^|!)SPIN $t [0-9]+$" "$tmp/spin.log.txt" | sed 's/.* //' | tr '\n' ' ')"
    [ "$got" = "0 1 2 3 4 5 " ]
    result $? "spin: task $t printed rounds 0..5 in order (${got:-none})"
done
no_crash spin

# ---- user faults ----------------------------------------------------
boot faults cortex-a72 "$image" "samos.run=KTEST.ELF ud;KTEST.ELF pf;KTEST.ELF kexec ffffffff80001000;KTEST.ELF bss" \
    '(^|!)BSS (OK|FAIL)' '^e2e: user selftest done' 'instruction abort, lower EL'
dump faults
expect faults '^USER FAULT: undefined instruction in @:/KTEST.ELF: ESR 0x2000000 ELR 0x4[0-9a-f]{5} FAR 0x[0-9a-f]+$' "udf: EC 0x0, ELR in the program"
expect faults '^USER FAULT: data abort, lower EL in @:/KTEST.ELF: ESR 0x92000044 ELR 0x4[0-9a-f]{5} FAR 0x700000000000$' "store to an unmapped page: EC 0x24, write, translation fault, FAR"
expect faults '^USER FAULT: instruction abort, lower EL in @:/KTEST.ELF: ESR 0x8200000f ELR 0xffffffff80001000 FAR 0xffffffff80001000$' "jump into kernel text: EC 0x20, permission fault"
expect_count faults '^process killed$' 3 "each fault killed its process"
expect faults '(^|!)BSS OK$' "another KTEST.ELF ran normally"
expect faults '^e2e: user selftest done$' "BLANK.ELF finished: the kernel kept running"
no_crash faults

# ---- bad pointers to syscalls ---------------------------------------
boot kptr cortex-a72 "$image" "samos.run=KTEST.ELF kptr;KTEST.ELF longpath" \
    '(^|!)KPTR (OK|FAIL)' '(^|!)LONGPATH (OK|FAIL)'
dump kptr
expect kptr '(^|!)KPTR print kernel string ok$' "print(kernel string) -EFAULT"
expect kptr '(^|!)KPTR fread into kernel text ok$' "fread into the kernel -EFAULT"
expect kptr '(^|!)KPTR syscall with kernel rsp ok$' "a kernel sp at the svc does not matter"
expect kptr '(^|!)KPTR print past the page ok$' "string into an unmapped page: -EFAULT (LDTRB fault, fixup)"
expect kptr '(^|!)KPTR fread past the page ok$' "buffer into an unmapped page: -EFAULT (STTRB fault, fixup)"
expect kptr '(^|!)KPTR fread inside the page ok$' "the same buffer inside the page works"
expect kptr '(^|!)KPTR OK$' "every kernel and bad pointer refused"
expect kptr '(^|!)LONGPATH OK$' "oversized paths and argument lists refused"
expect_no kptr 'KPTR .* FAIL' "no pointer check failed"
no_crash kptr

# ---- waitpid -------------------------------------------------------
boot parent cortex-a72 "$image" "samos.run=KTEST.ELF parent" '(^|!)PARENT (OK|FAIL)$'
dump parent
expect parent '(^|!)PARENT waitpid into kernel memory ok -20$' "waitpid with a kernel status pointer: -EFAULT"
expect parent '(^|!)PARENT exit\(42\) status ok 42$' "exit(42)"
expect parent '(^|!)PARENT main returning 7 ok 7$' "main's return value"
expect parent '(^|!)PARENT undefined instruction is 128 \+ 6 ok 134$' "a fault: 128 + 6"
expect parent '(^|!)PARENT page fault is 128 \+ 14 ok 142$' "an abort: 128 + 14"
expect parent '(^|!)PARENT killed status is 128 \+ 9 ok 137$' "killed: 128 + 9"
expect parent '(^|!)PARENT OK$' "waitpid, kill, getpid all as on x86"
no_crash parent

# ---- ELF machine check ---------------------------------------------
# HELLO.ELF with e_machine (offset 18, little endian) set to 243,
# EM_RISCV, as RISCV.ELF.
bad="$tmp/badelf.img"
cp "$image" "$bad"
MTOOLS_SKIP_CHECK=1 mcopy -i "$bad" ::/HELLO.ELF "$tmp/riscv.elf"
printf '\363\000' | dd of="$tmp/riscv.elf" bs=1 seek=18 conv=notrunc status=none
MTOOLS_SKIP_CHECK=1 mcopy -i "$bad" "$tmp/riscv.elf" ::/RISCV.ELF
boot badelf cortex-a72 "$bad" "samos.run=KTEST.ELF badelf X86BLANK.ELF RISCV.ELF;HELLO.ELF ok" \
    '(^|!)BADELF (DONE|FAIL)' '(^|!)hello: argv\[1\]=ok'
dump badelf
expect badelf '(^|!)BADELF X86BLANK.ELF rejected ' "EM_X86_64 refused by syscall 7"
expect badelf '(^|!)BADELF RISCV.ELF rejected ' "EM_RISCV refused by syscall 7"
expect badelf '(^|!)BADELF DONE$' "nothing ran"
expect badelf '(^|!)hello: argv\[1\]=ok$' "the same ELF as EM_AARCH64 runs"
no_crash badelf

# ---- PAN -----------------------------------------------------------
boot pan max "$image" "samos.run=KTEST.ELF kptr;KTEST.ELF regs 3" '(^|!)KPTR (OK|FAIL)' '(^|!)REGS (OK|FAIL) 3'
dump pan
expect pan '^cpu: PAN on \(PSTATE.PAN 1, SPAN 0\)$' "-cpu max: PAN on"
expect pan '(^|!)KPTR OK$' "uaccess (LDTR/STTR) works under PAN"
expect pan '(^|!)REGS OK 3$' "registers intact on -cpu max"
no_crash pan

# ---- kernel_selftest and BLANK.ELF's markers --------------------------
# The shared feature self-tests (kernel_selftest, memtest) and BLANK.ELF
# write boot markers (bootmarker.h), on aarch64 a .bss page whose
# physical address the boot prints. Dumped with the monitor's pmemsave
# once BLANK.ELF is done, as tests64/e2e does at 0x6000 on x86. Every
# feature marker reached must say 1 (pass), except the ones for devices
# aarch64 has no driver or bus for before the aarch64 device work and two that hold values.
markers_log="$tmp/markers.log"
markers_bin="$tmp/markers.bin"
{
    for _ in $(seq 1 $((timeout_s * 10))); do
        grep -aq 'e2e: user selftest done' "$markers_log" 2>/dev/null && break
        sleep 0.1
    done
    pa="$(tr -d '\r' < "$markers_log" | sed -nE 's/.*boot markers at pa (0x[0-9a-f]+)$/\1/p')"
    printf 'pmemsave %s 4096 "%s"\n' "${pa:-0}" "$markers_bin"
    sleep 1
    printf 'quit\n'
} | qemu-system-aarch64 -machine virt,gic-version=3 -cpu cortex-a72 -m 512M -display none \
    -kernel "$root/bin/aarch64/kernel.img" -initrd "$image" -append "samos.test=1" \
    -serial "file:$markers_log" -monitor stdio > /dev/null 2>&1
allowed=" PCI_IDE PCI_VGA PCI_BARS PCI_FN_COUNT PCI_HOSTBR PCI_SAMOS_VEND NVME_PRESENT DISK_MULTIPLE PS2_MOUSE MEM_RAM_MIB MEM_PROCESS_LEAK "
pass=0 bad="" devices=""
if [ -s "$markers_bin" ]; then
    while read -r name stage; do
        lo="$(od -An -v -tx4 -j $((stage * 8)) -N 4 "$markers_bin" | tr -d ' ')"
        hi="$(od -An -v -tx4 -j $((stage * 8 + 4)) -N 4 "$markers_bin" | tr -d ' ')"
        [ "$((16#$hi))" -eq $((0xB007C0DE)) ] && [ "$(((16#$lo >> 24) & 0xff))" -eq "$stage" ] || continue
        value=$((16#$lo & 0xffffff))
        short="${name#BM_FEATURE_}"
        short="${short#BM_USER_}"
        if [ "$value" -eq 1 ]; then
            pass=$((pass + 1))
        elif [ "${allowed#* $short }" != "$allowed" ]; then
            devices="$devices $short=$value"
        else
            bad="$bad $short=$value"
        fi
    done < <(sed -nE 's/^ *(BM_(FEATURE|USER)_[A-Z0-9_]+) *= *([0-9]+),.*/\1 \3/p' "$root/src/bootmarker.h")
fi
echo "markers: $pass pass; device or value markers:$devices; other:${bad:- none}"
[ "$pass" -ge 100 ] && [ -z "$bad" ]
result $? "markers: kernel_selftest, memtest and BLANK.ELF features pass ($pass), only device markers do not"
tr -d '\r' < "$markers_log" > "$tmp/markers.log.txt"
no_crash markers

# ---- a fault without room for its frame -----------------------------
boot stack cortex-a72 "$image" "samos.double_fault_test" '^exception: kernel stack overflow, halted'
dump stack
expect stack '^exception: sync \(current EL, SP_ELx\) on a bad kernel stack, sp 0x7ff000000000$' "reported from a fresh stack"
expect stack '^  ESR 0x96000044 \(EC 0x25, data abort\)$' "the store's data abort"
expect stack '^exception: kernel stack overflow, halted$' "halted"

echo "aarch64 arch test: $checks checks, $failures failures"
[ $failures -eq 0 ]
