#!/usr/bin/env bash
# e2e/140-early-fault.sh - a CPU exception before idt_init()
# lands in the early IDT and dumps registers on COM1 instead of
# resetting the machine. CMDLINE.TXT on the ESP carries
# samos.early_fault_test, which makes kernel_main execute ud2.
. "$(dirname "$0")/_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.early_fault_test\n' > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

boot_and_dump 20 '^System halted'
expect_serial '^boot_info: cmdline "samos.early_fault_test"$' "loader passed the command line"
expect_serial '^EARLY FAULT: vector 0x0000000000000006 ' "#UD reported by the early IDT"
expect_serial '^  rip 0xffffffff801' "register dump with a kernel rip (higher half)"
expect_no_serial 'Hello 64-bit' "kernel stopped at the fault"
echo "PASS: e2e/140 early fault"
