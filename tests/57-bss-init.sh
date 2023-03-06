#!/bin/bash
# tests/57-bss-init.sh
#
# Ch 132 + G12 - behavioural test for the ELF loader's .bss handling.
#
# kernel_main loads a blank.elf instance with argv[0]="BSS". Its main
# does `static int bss_counter; bss_counter++;` then prints "BSS-OK"
# if bss_counter == 1 (the static started zero-initialised, as ELF
# .bss requires) and "BSS-FAIL" otherwise.
#
# Before G12: process_map_elf mapped the .bss PHDR to elf_buffer + 0,
# aliasing the start of the in-memory ELF file. The static read back
# as 0x464c457f (little-endian "ELF\x7F") and the increment never
# landed on a true 1, so "BSS-FAIL" printed.
#
# After G12: process_map_elf detours .bss-shaped PHDRs through a
# fresh kzalloc'd buffer, so the static starts at 0 and increments
# to exactly 1.

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log" 'BSS-OK' 'Abc!'

if grep -q 'BSS-FAIL' "$log"; then
    echo "FAIL: 'BSS-FAIL' in serial - .bss did NOT start zero-initialised (G12 fix missing or broken)"
    head -30 "$log"
    exit 1
fi

grep -q 'BSS-OK' "$log" || {
    echo "FAIL: neither 'BSS-OK' nor 'BSS-FAIL' in serial - BSS task crashed before printing"
    head -30 "$log"
    exit 1
}
exit 0
