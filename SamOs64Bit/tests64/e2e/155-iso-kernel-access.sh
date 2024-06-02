#!/usr/bin/env bash
# e2e/155-iso-kernel-access.sh - user code cannot touch
# kernel memory. Five KTEST.ELF runs each fault on a kernel address
# and are killed; the kernel keeps running and BLANK.ELF completes.
#   - write to the kernel text (0xFFFFFFFF80105000): present but
#     supervisor-only, error 7
#   - write to the boot markers through the direct map: error 7
#   - read from the direct map: error 5
#   - jump into kernel text: error 0x15 (instruction fetch)
#   - write to physical 0x100000 at its old identity address, which
#     every process used to map user-writable:
#     not mapped at all now, error 6
# Parity: aarch64 the same five with its addresses; a permission fault
# (DFSC/IFSC 0xf) where x86 has error 7, 5 or 0x15, a translation
# fault where it has 6.
. "$(dirname "$0")/_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
case "$E2E_ARCH" in
aarch64)
    # The kernel image at 0xffffffff80000000 (pa 0x40200000), the
    # direct map at 0xffff800000000000 + pa (its alias of the image and
    # of free RAM above it), no kernel mapping in the low half.
    printf 'samos.run=KTEST.ELF kwrite ffffffff80001000;KTEST.ELF kwrite ffff800040201000;KTEST.ELF kread ffff800040400000;KTEST.ELF kexec ffffffff80001000;KTEST.ELF kwrite 40200000\n' ;;
riscv64)
    # The kernel is at 0xffffffff80200000 (pa 0x80200000), the
    # Sv48 direct map at 0xffff800000000000; its text alias and free
    # RAM are present, supervisor-only. Under Sv39 those direct-map
    # addresses are not canonical, which faults the same way. The low
    # half has no kernel mapping at all (the riscv64 MMU work removed the identity map).
    printf 'samos.run=KTEST.ELF kwrite ffffffff80201000;KTEST.ELF kwrite ffff800080200000;KTEST.ELF kread ffff800080400000;KTEST.ELF kexec ffffffff80201000;KTEST.ELF kwrite 80200000\n' ;;
*)
    printf 'samos.run=KTEST.ELF kwrite ffffffff80105000;KTEST.ELF kwrite ffff800000006000;KTEST.ELF kread ffff800000100000;KTEST.ELF kexec ffffffff80105000;KTEST.ELF kwrite 100000\n' ;;
esac > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

boot_and_dump 40
case "$E2E_ARCH" in
aarch64)
    # ESR: EC 0x24 data abort (0x20 instruction abort), IL, WnR for a
    # store; DFSC 0xf permission fault level 3, 0x5 translation fault
    # level 1.
    F='^USER FAULT: data abort, lower EL in @:/KTEST.ELF: ESR'
    expect_serial "$F 0x9200004f ELR 0x4[0-9a-f]{5} FAR 0xffffffff80001000\$" "write to the kernel image refused"
    expect_serial "$F 0x9200004f ELR 0x4[0-9a-f]{5} FAR 0xffff800040201000\$" "write through the direct map refused"
    expect_serial "$F 0x9200000f ELR 0x4[0-9a-f]{5} FAR 0xffff800040400000\$" "read through the direct map refused"
    expect_serial '^USER FAULT: instruction abort, lower EL in @:/KTEST.ELF: ESR 0x8200000f ELR 0xffffffff80001000 FAR 0xffffffff80001000$' "jump into kernel text refused"
    expect_serial "$F 0x92000045 ELR 0x4[0-9a-f]{5} FAR 0x40200000\$" "no kernel mapping in the low half" ;;
riscv64)
    F='^USER FAULT: scause 0x[df] \((load|store) page fault\) in @:/KTEST.ELF: sepc 0x4[0-9a-f]+'
    expect_serial "$F stval 0xffffffff80201000$" "write to the kernel image refused"
    expect_serial "$F stval 0xffff800080200000$" "write through the direct map refused"
    expect_serial '^USER FAULT: scause 0xd \(load page fault\) in @:/KTEST.ELF: sepc 0x4[0-9a-f]+ stval 0xffff800080400000$' "read through the direct map refused"
    expect_serial '^USER FAULT: scause 0xc \(instruction page fault\) in @:/KTEST.ELF: sepc 0xffffffff80201000 stval 0xffffffff80201000$' "jump into kernel text refused"
    expect_serial "$F stval 0x80200000$" "no kernel mapping in the low half" ;;
*)
    F='^USER FAULT: #PF in @:/KTEST.ELF: rip 0x00000000004[0-9a-f]+'
    expect_serial "$F error 0x0000000000000007 cr2 0xffffffff80105000$" "write to the kernel image refused"
    expect_serial "$F error 0x0000000000000007 cr2 0xffff800000006000$" "write through the direct map refused"
    expect_serial "$F error 0x0000000000000005 cr2 0xffff800000100000$" "read through the direct map refused"
    expect_serial '^USER FAULT: #PF in @:/KTEST.ELF: rip 0xffffffff80105000 error 0x0000000000000015 cr2 0xffffffff80105000$' "jump into kernel text refused"
    expect_serial "$F error 0x0000000000000006 cr2 0x0000000000100000$" "old identity map of the kernel is gone" ;;
esac
[ "$(tr -d '\r' < "$E2E_SERIAL" | grep -c '^process killed$')" -ge 5 ] \
    || e2e_fail "expected five killed processes"
expect_no_serial 'KTEST survived' "no access went through"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT' "no kernel panic"
expect_stage_value 41 -eq 1 "BLANK.ELF fread after the kills"
expect_stage_value 46 -eq 1 "BLANK.ELF realloc after the kills"
echo "PASS: e2e/155 user access to kernel memory"
