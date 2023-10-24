#!/usr/bin/env bash
# e2e/156-iso-kernel-pointer.sh - kernel and bad pointers
# passed to syscalls are refused with -EFAULT (or the call's own
# failure value) and the kernel neither reads nor writes through them.
# KTEST.ELF kptr covers print, fopen, fread, fstat, get_arguments,
# system, realloc, free, window events, graphics handles, a syscall
# made with rsp pointing into the kernel, file descriptors the process
# does not own, and strings and buffers that run into an unmapped page.
# The process keeps running and exits normally.
. "$(dirname "$0")/_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.run=KTEST.ELF kptr\n' > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

boot_and_dump 40
expect_serial 'KPTR still printing$' "the process is still served after the bad calls"
expect_serial 'KPTR OK$' "every bad pointer refused"
expect_no_serial 'KPTR .* FAIL' "no check failed"
expect_no_serial 'USER FAULT|process killed' "no fault: the kernel caught every pointer"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT' "no kernel panic"
# fread into the boot markers through the direct map was refused, so
# the markers BLANK.ELF writes afterwards are intact.
expect_stage_value 41 -eq 1 "BLANK.ELF fread afterwards"
expect_stage_value 46 -eq 1 "BLANK.ELF realloc afterwards"
echo "PASS: e2e/156 kernel pointers to syscalls"
