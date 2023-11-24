#!/usr/bin/env bash
# e2e/182-user-file-syscalls.sh - the file syscalls
# 40-46 (fwrite, ftruncate, unlink, mkdir, rmdir, opendir, readdir)
# and the fopen/fclose changes, from user mode. KTEST.ELF files checks
# the error statuses (ENOENT, EEXIST, ENOTDIR, EISDIR, ENOTEMPTY,
# ERDONLY), kernel pointers refused with EFAULT, a write buffer and a
# readdir entry that cross a page boundary, a write whose buffer runs
# past the end of its malloc block into the unmapped page after it
# refused whole (the file keeps its size), fclose
# really closing (600 opens through 512 descriptors), and directory
# descriptors a process leaves open closed on exit (two children
# that each leave 300 open).
. "$(dirname "$0")/_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.run=KTEST.ELF files\n' > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

E2E_ALSO_WAIT='FILES (OK|FAIL)' boot_and_dump 60
expect_no_serial 'FILES .* FAIL|OPENDIRS FAIL' "no check failed"
expect_serial 'OPENDIRS 300 open' "a child left 300 directories open"
expect_serial 'FILES OK' "every file syscall check passed"
expect_no_serial 'USER FAULT|process killed' "no fault"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT' "no kernel panic"
echo "PASS: e2e/182 user file syscalls"
