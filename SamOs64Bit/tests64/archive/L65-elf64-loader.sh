#!/bin/bash
# tests64/L65-elf64-loader.sh
#
# Lecture 65 - ELF loader refactored to ELF64.
#
# elf.h: all elf32_* types and structs renamed elf64_* and
# widened. struct elf_header fields use new types. struct
# elf64_phdr has reordered fields (p_flags moves to 2nd slot
# for natural 8-byte alignment).
#
# elf_valid_class now accepts ELFCLASS64 (was 32).
#
# elf_process_phdr_pt_load zero-fills the gap between p_filesz
# and p_memsz (.bss).
#
# task_init's ELF branch now sets ip = elf_header()->e_entry
# (no longer panics).
#
# kernel still loads SIMPLE.BIN to keep the test fast (L66
# will flip to BLANK.ELF).

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

dump=$(mktemp)
trap 'rm -f "$dump"' EXIT

( sleep 5; printf 'pmemsave 0xb8000 4096 "%s"\nquit\n' "$dump" ) | timeout 12 \
    qemu-system-x86_64 -hda bin/os.bin -m 256 -accel tcg -display none -vga std \
        -monitor stdio -no-reboot > /dev/null 2>&1

chars=$(od -An -v -tx1 -w1 "$dump" \
            | awk 'NR%2==1 {print $1}' \
            | xxd -r -p \
            | tr '\0' ' ')

ok=1
for tok in 'tss ready' 'user enter'; do
    echo "$chars" | grep -q "$tok" || { echo "FAIL: missing '$tok'"; ok=0; }
done

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
