#!/bin/bash
# tests64/L66-blank-elf-loadable.sh
#
# Lecture 66 - blank.elf is a real ELF64, and the L65 ELF
# loader code is wired through.
#
# Test scope (NOT a runtime test of blank.elf):
#   1. programs/blank/blank.c compiles via x86_64-elf-gcc.
#   2. The output blank.elf is ELF64 / x86_64 / EXEC.
#   3. Entry point >= 0x400000 (SAMOS_PROGRAM_VIRTUAL_ADDRESS).
#   4. The ELF has a PT_LOAD program header.
#   5. kernel still boots to "user enter" (SIMPLE.BIN path,
#      not BLANK.ELF; the latter would take 40+ sec to PIO-read
#      in QEMU TCG).

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

ELF=programs/blank/blank.elf
[ -f "$ELF" ] || { echo "FAIL: $ELF missing"; exit 1; }

file "$ELF" | grep -q 'ELF 64-bit' \
    || { echo "FAIL: $ELF not ELF64"; exit 1; }
file "$ELF" | grep -q 'x86-64' \
    || { echo "FAIL: $ELF not x86-64"; exit 1; }
file "$ELF" | grep -q 'executable' \
    || { echo "FAIL: $ELF not executable"; exit 1; }

# Entry must be >= 0x400000.
PATH="$HOME/opt/cross/bin:$PATH"
entry=$(x86_64-elf-readelf -h "$ELF" | awk '/Entry/ {print $NF}')
[ "$(printf '%d' "$entry")" -ge "$((0x400000))" ] \
    || { echo "FAIL: entry $entry below SAMOS_PROGRAM_VIRTUAL_ADDRESS"; exit 1; }

x86_64-elf-readelf -l "$ELF" | grep -q 'LOAD' \
    || { echo "FAIL: no LOAD segment in $ELF"; exit 1; }

# Kernel still boots cleanly via the SIMPLE.BIN path.
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
