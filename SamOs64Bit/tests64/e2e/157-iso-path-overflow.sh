#!/usr/bin/env bash
# e2e/157-iso-path-overflow.sh - the book strcpy'd the
# name from syscall 6 into a 108-byte kernel stack buffer
# (a kernel stack overflow). KTEST.ELF longpath sends 300-byte names to
# syscalls 6 and 10, the 104/105-byte boundary of "@:/" + name, an
# unterminated 512-byte argument and a looping argument list to
# syscall 7, and a 600-byte print. Each is refused or cut short.
. "$(dirname "$0")/_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.run=KTEST.ELF longpath\n' > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

boot_and_dump 40
expect_serial '^KPTR load_start 300-byte name ok$' "syscall 6 refuses a 300-byte name"
expect_serial '^KPTR load_start 105-byte name ok$' "syscall 6 refuses one byte over the buffer"
expect_serial '^KPTR system looping argument list ok$' "syscall 7 bounds the argument list"
expect_serial '^LONGPATH OK$' "all oversized input refused"
expect_no_serial 'KPTR .* FAIL' "no check failed"
expect_no_serial 'USER FAULT|process killed' "no fault"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT' "no kernel panic"
expect_stage_value 41 -eq 1 "BLANK.ELF fread afterwards"
echo "PASS: e2e/157 path buffer overflow"
