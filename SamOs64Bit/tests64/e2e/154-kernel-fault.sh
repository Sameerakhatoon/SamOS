#!/usr/bin/env bash
# e2e/154-kernel-fault.sh - a fault in kernel mode, after
# idt_init, is a panic with a register dump. A fault that cannot push
# its frame (rsp unmapped) becomes a #DF, which runs on its own IST
# stack and dumps too instead of triple-faulting. Also checks the
# CR0/CR4/EFER line from cpu_init.
. "$(dirname "$0")/_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
set_cmdline() {
    printf '%s\n' "$1" > "$E2E_TMP/CMDLINE.TXT"
    MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
        || e2e_fail "mcopy CMDLINE.TXT"
}

set_cmdline samos.kernel_fault_test
boot_and_dump 20 'KERNEL PANIC'
# cr0: PE, MP, ET (hardwired), NE, WP, PG set, EM and TS clear;
# cr4: OSFXSR, OSXMMEXCPT; efer: LME, LMA, NXE
expect_serial '^cpu: cr0 0x0000000080010033 cr4 0x[0-9a-f]{13}6[0-9a-f]{2} efer 0x0000000000000d00$' "cpu_init set CR0, CR4 and EFER"
expect_serial '^KERNEL FAULT: #UD vector 0x0000000000000006 error 0x0000000000000000$' "#UD in kernel mode reported"
expect_serial '^  rip 0x00000000001[0-9a-f]{5} cs 0x0000000000000018 ' "register dump with a kernel rip and cs"
expect_serial '^  r13 0x' "dump covers r8-r15"
expect_serial 'KERNEL PANIC: CPU exception in kernel mode' "kernel fault panics"
expect_no_serial 'EARLY FAULT' "the full IDT handled it, not the early one"

set_cmdline samos.double_fault_test
boot_and_dump 20 'KERNEL PANIC'
expect_serial '^KERNEL FAULT: #DF vector 0x0000000000000008 ' "#DF reported"
expect_serial '^  rsp 0x00007ff000000000 ' "the #DF frame holds the bad rsp (handler ran on its IST stack)"
expect_serial 'KERNEL PANIC: CPU exception in kernel mode' "double fault panics"
echo "PASS: e2e/154 kernel fault"
