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
# e2e-requires: x86 (x86 exception names, error codes and registers in the report; aarch64-traps.sh and aarch64-user.sh check the ESR/ELR/FAR reports)
. "$(dirname "$0")/_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.run=KTEST.ELF kwrite ffffffff80105000;KTEST.ELF kwrite ffff800000006000;KTEST.ELF kread ffff800000100000;KTEST.ELF kexec ffffffff80105000;KTEST.ELF kwrite 100000\n' \
    > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

boot_and_dump 40
F='^USER FAULT: #PF in @:/KTEST.ELF: rip 0x00000000004[0-9a-f]+'
expect_serial "$F error 0x0000000000000007 cr2 0xffffffff80105000$" "write to the kernel image refused"
expect_serial "$F error 0x0000000000000007 cr2 0xffff800000006000$" "write through the direct map refused"
expect_serial "$F error 0x0000000000000005 cr2 0xffff800000100000$" "read through the direct map refused"
expect_serial '^USER FAULT: #PF in @:/KTEST.ELF: rip 0xffffffff80105000 error 0x0000000000000015 cr2 0xffffffff80105000$' "jump into kernel text refused"
expect_serial "$F error 0x0000000000000006 cr2 0x0000000000100000$" "old identity map of the kernel is gone"
[ "$(tr -d '\r' < "$E2E_SERIAL" | grep -c '^process killed$')" -ge 5 ] \
    || e2e_fail "expected five killed processes"
expect_no_serial 'KTEST survived' "no access went through"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT' "no kernel panic"
expect_stage_value 41 -eq 1 "BLANK.ELF fread after the kills"
expect_stage_value 46 -eq 1 "BLANK.ELF realloc after the kills"
echo "PASS: e2e/155 user access to kernel memory"
