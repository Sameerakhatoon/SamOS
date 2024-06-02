#!/usr/bin/env bash
# e2e/159-iso-stack-overflow.sh - the 16 KiB user stack
# (0x3FB000-0x3FF000) has an unmapped guard page below it. Unbounded
# recursion in KTEST.ELF stack faults there, the process is killed and
# the kernel keeps running. Before the isolation work the memory below the stack was
# writable RAM, so an overflow silently corrupted it.
. "$(dirname "$0")/_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.run=KTEST.ELF stack\n' > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

boot_and_dump 40
expect_serial 'KTEST stack: recursing$' "KTEST started recursing"
case "$E2E_ARCH" in
aarch64)
    # EC 0x24, WnR, DFSC 7 (translation fault, level 3) in the guard page
    expect_serial '^USER FAULT: data abort, lower EL in @:/KTEST.ELF: ESR 0x92000047 ELR 0x4[0-9a-f]{5} FAR 0x3fa[0-9a-f]{3}$' "data abort in the guard page" ;;
riscv64)
    # A store (the call's frame) into the guard page
    expect_serial '^USER FAULT: scause 0xf \(store page fault\) in @:/KTEST.ELF: sepc 0x4[0-9a-f]+ stval 0x3fa[0-9a-f]{3}$' "store page fault in the guard page" ;;
*)
    # error 6 = user write to a not-present page, cr2 inside the guard page
    expect_serial '^USER FAULT: #PF in @:/KTEST.ELF: rip 0x00000000004[0-9a-f]+ error 0x0000000000000006 cr2 0x00000000003fa[0-9a-f]{3}$' "#PF in the guard page" ;;
esac
expect_serial '^process killed$' "overflowing process killed"
expect_no_serial 'KTEST survived' "the recursion did not continue"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT' "no kernel panic"
expect_stage_value 41 -eq 1 "BLANK.ELF fread after the kill"
echo "PASS: e2e/159 user stack overflow"
