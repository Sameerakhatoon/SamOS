#!/usr/bin/env bash
# e2e/160-iso-nx-wx.sh - NX on stack, heap and data
# pages and W^X on ELF segments. KTEST.ELF bss first checks that .bss
# is zero and writable in its own segment. Then four
# runs each fault and are killed: executing from the stack, the heap
# and .data (error 0x15, instruction fetch from a present NX page)
# and writing to its own code (error 7).
. "$(dirname "$0")/_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.run=KTEST.ELF bss;KTEST.ELF nxstack;KTEST.ELF nxheap;KTEST.ELF nxdata;KTEST.ELF wtext\n' \
    > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

boot_and_dump 40
expect_serial '^BSS OK$' ".bss zeroed and writable"
expect_serial '^USER FAULT: #PF in @:/KTEST.ELF: rip 0x00000000003f[0-9a-f]{4} error 0x0000000000000015 cr2 0x00000000003f[0-9a-f]{4}$' "stack is NX"
expect_serial '^USER FAULT: #PF in @:/KTEST.ELF: rip 0x00000000100[0-9a-f]{5} error 0x0000000000000015 cr2 0x00000000100[0-9a-f]{5}$' "heap is NX"
expect_serial '^USER FAULT: #PF in @:/KTEST.ELF: rip 0x00000000004[0-9a-f]{5} error 0x0000000000000015 cr2 0x00000000004[0-9a-f]{5}$' ".data is NX"
expect_serial '^USER FAULT: #PF in @:/KTEST.ELF: rip 0x00000000004[0-9a-f]{5} error 0x0000000000000007 cr2 0x00000000004[0-9a-f]{5}$' "code is read-only"
[ "$(tr -d '\r' < "$E2E_SERIAL" | grep -c '^process killed$')" -ge 4 ] \
    || e2e_fail "expected four killed processes"
expect_no_serial 'KTEST survived|BSS FAIL' "nothing ran from a data page"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT' "no kernel panic"
expect_stage_value 41 -eq 1 "BLANK.ELF fread after the kills"
echo "PASS: e2e/160 NX and W^X"
