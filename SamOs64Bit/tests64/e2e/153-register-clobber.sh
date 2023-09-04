#!/usr/bin/env bash
# e2e/153-register-clobber.sh - all 15 general registers and
# xmm0-15 survive a syscall and a task switch. Two KTEST.ELF instances
# with different seeds load every register with seed-specific values,
# then make a syscall that returns directly (sum) and one that sleeps
# and switches to the other tasks (udelay), 20 rounds each. BLANK.ELF
# runs alongside as a third task.
. "$(dirname "$0")/_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.run=KTEST.ELF regs 1;KTEST.ELF regs 2\n' > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

_wait_all() {
    e2e_wait_serial '^REGS (OK|FAIL) 1' 150 || true
    e2e_wait_serial '^REGS (OK|FAIL) 2' 150 || true
    e2e_wait_serial "$E2E_SENTINEL" 150 || true
    sleep 1
    printf 'quit\n'
}
e2e_run_qemu 480 _wait_all

expect_serial '^samos.run: loaded @:/KTEST.ELF$' "KTEST.ELF loaded from the command line"
expect_no_serial '^REGS FAIL' "no register changed across a syscall or task switch"
expect_serial '^REGS OK 1$' "task 1: 15 GPRs and xmm0-15 intact"
expect_serial '^REGS OK 2$' "task 2: 15 GPRs and xmm0-15 intact"
expect_serial "$E2E_SENTINEL" "BLANK.ELF finished alongside"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT|USER FAULT' "no fault"
echo "PASS: e2e/153 register clobber"
