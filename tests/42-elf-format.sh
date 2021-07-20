#!/bin/bash
# tests/42-elf-format.sh
#
# Ch 123 - the blank user program now builds as an ELF executable
# (blank.elf, OUTPUT_FORMAT(elf32-i386)). Verify the artifact is actually
# ELF and has the basics the kernel ELF loader checks (Ch 121-122):
#   - 0x7F 'E' 'L' 'F' signature
#   - ELFCLASS32 (e_ident[EI_CLASS] = 1)
#   - ELFDATA2LSB (e_ident[EI_DATA] = 1)
#   - ET_EXEC (e_type = 2)
#   - at least one program header (e_phoff != 0)
#   - entry point at or above SAMOS_PROGRAM_VIRTUAL_ADDRESS (0x400000)

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

elf=programs/blank/blank.elf
[ -f "$elf" ] || { echo "FAIL: $elf does not exist"; exit 1; }

# Signature: first 4 bytes
sig=$(xxd -p -l 4 "$elf")
[ "$sig" = "7f454c46" ] || { echo "FAIL: bad ELF signature: 0x$sig (want 0x7f454c46)"; exit 1; }

# Class (offset 4, 1 byte): 1 = ELFCLASS32
cls=$(xxd -p -s 4 -l 1 "$elf")
[ "$cls" = "01" ] || { echo "FAIL: not ELFCLASS32: 0x$cls"; exit 1; }

# Data (offset 5, 1 byte): 1 = ELFDATA2LSB
dat=$(xxd -p -s 5 -l 1 "$elf")
[ "$dat" = "01" ] || { echo "FAIL: not ELFDATA2LSB: 0x$dat"; exit 1; }

# Type (offset 16, 2 bytes LE): 2 = ET_EXEC
typ=$(xxd -p -s 16 -l 2 "$elf")
[ "$typ" = "0200" ] || { echo "FAIL: not ET_EXEC: 0x$typ (LE)"; exit 1; }

# Entry point (offset 24, 4 bytes LE) - should be >= 0x400000
ep_bytes=$(xxd -p -s 24 -l 4 "$elf")
ep_le=$(echo "$ep_bytes" | sed 's/\(..\)\(..\)\(..\)\(..\)/\4\3\2\1/')
ep_dec=$((16#$ep_le))
if [ "$ep_dec" -lt $((16#400000)) ]; then
    echo "FAIL: entry point 0x$ep_le below 0x00400000"
    exit 1
fi

# Program header offset (offset 28, 4 bytes LE) - should be non-zero
phoff_bytes=$(xxd -p -s 28 -l 4 "$elf")
phoff_le=$(echo "$phoff_bytes" | sed 's/\(..\)\(..\)\(..\)\(..\)/\4\3\2\1/')
phoff_dec=$((16#$phoff_le))
[ "$phoff_dec" -ne 0 ] || { echo "FAIL: e_phoff is zero (no program header)"; exit 1; }

# At least one program header (offset 44, 2 bytes LE)
phnum_bytes=$(xxd -p -s 44 -l 2 "$elf")
phnum_le=$(echo "$phnum_bytes" | sed 's/\(..\)\(..\)/\2\1/')
phnum_dec=$((16#$phnum_le))
[ "$phnum_dec" -ge 1 ] || { echo "FAIL: e_phnum is 0"; exit 1; }

exit 0
