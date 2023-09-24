#!/usr/bin/env bash
# e2e/158-iso-malformed-elf.sh - the ELF loader refuses
# malformed files. make_bad_elf.py writes copies of KTEST.ELF with one
# bad field each (type, machine, entry, phdr table, segment bounds,
# kernel or low p_vaddr, a W+X segment, overlapping segments, a
# truncated file). KTEST.ELF badelf asks syscall 7 to run each; one
# is also named in samos.run= so the kernel's own load path is
# covered. Nothing may start (a wrongly accepted copy would print
# "KTEST HELLO") and the kernel keeps running.
. "$(dirname "$0")/_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
P2="$E2E_IMAGE@@$((716800 * 512))"
MTOOLS_SKIP_CHECK=1 mcopy -i "$P2" ::/KTEST.ELF "$E2E_TMP/KTEST.ELF" || e2e_fail "mcopy KTEST.ELF out"
mkdir -p "$E2E_TMP/bad"
names=$(python3 "$(dirname "$0")/make_bad_elf.py" "$E2E_TMP/KTEST.ELF" "$E2E_TMP/bad") \
    || e2e_fail "make_bad_elf.py"
for n in $names; do
    MTOOLS_SKIP_CHECK=1 mcopy -o -i "$P2" "$E2E_TMP/bad/$n" "::/$n" || e2e_fail "mcopy $n"
done
printf 'samos.run=BADWX.ELF;KTEST.ELF badelf %s\n' "$(echo $names)" > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

boot_and_dump 60
expect_serial '^samos.run: failed to load @:/BADWX.ELF$' "the kernel refuses a W+X segment"
for n in $names; do
    expect_serial "^BADELF $n rejected " "$n refused"
done
expect_serial '^BADELF DONE$' "every malformed file refused"
expect_no_serial 'ACCEPTED|KTEST HELLO' "no malformed file ran"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT|USER FAULT' "no fault"
expect_stage_value 41 -eq 1 "BLANK.ELF fread afterwards"
echo "PASS: e2e/158 malformed ELF"
