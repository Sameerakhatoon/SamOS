#!/bin/bash
# tests64/L62-stdlib-64bit.sh
#
# Lecture 62 - userland stdlib rebuilt for 64-bit.
#
# Checks:
#   1. stdlib.elf, blank.elf, shell.elf all build under
#      x86_64-elf-ld and report ELF64 in `file`.
#   2. Kernel still boots and reaches "user enter".

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

for elf in programs/stdlib/stdlib.elf programs/blank/blank.elf programs/shell/shell.elf; do
    [ -f "$elf" ] || { echo "FAIL: $elf missing"; exit 1; }
    file "$elf" | grep -q 'ELF 64-bit' \
        || { echo "FAIL: $elf is not ELF 64-bit"; exit 1; }
done

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
