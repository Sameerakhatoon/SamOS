#!/bin/bash
# tests64/L35-idt-pushad-macros.sh
#
# Lecture 35 - 64-bit pushad / popad macros + idt.asm migration
# start.
#
# x86_64 dropped the 1-insn pushad/popad. idt.asm now defines
# pushad_macro / popad_macro that stash rsp, push the same set
# of registers in the same order at 64-bit width, and restore
# in reverse. The rest of idt.asm (push esp, iret, the
# interrupt_pointer_table dd entries) is still 32-bit; L36
# finishes the migration.
#
# idt.asm is NOT in the build yet. This test just confirms the
# tree builds and boots; idt.asm staying out of FILES means a
# broken macro would still let us boot.

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

dump=$(mktemp)
trap 'rm -f "$dump"' EXIT

( sleep 2; printf 'pmemsave 0xb8000 4096 "%s"\nquit\n' "$dump" ) | timeout 15 \
    qemu-system-x86_64 -hda bin/os.bin -m 256 -accel tcg -display none -vga std \
        -monitor stdio -no-reboot > /dev/null 2>&1

chars=$(od -An -v -tx1 -w1 "$dump" \
            | awk 'NR%2==1 {print $1}' \
            | xxd -r -p \
            | tr '\0' ' ')

ok=1
for tok in 'Hello 64-bit!' 'e820 total:' 'ABC' 'multiheap ready'; do
    echo "$chars" | grep -q "$tok" || { echo "FAIL: missing '$tok'"; ok=0; }
done

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
