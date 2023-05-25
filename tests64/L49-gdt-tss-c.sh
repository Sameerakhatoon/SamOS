#!/bin/bash
# tests64/L49-gdt-tss-c.sh
#
# Lecture 49 - GDT + TSS C side rebuilt for 64-bit.
#
# gdt.{h,c} rewritten:
#   struct gdt   -> struct gdt_entry (rename)
#   NEW struct tss_desc_64 (16 bytes, two GDT slots)
#   NEW gdt_set(entry, address, limit_low, access, flags)
#   NEW gdt_set_tss(desc, tss_addr, limit, type, flags)
# OLD gdt_structured_to_gdt + encodeGdtEntry removed.
#
# TSS_DESCRIPTOR_TYPE (= 0x89) added to config.h.
#
# Nothing actually calls gdt_set / gdt_set_tss yet. L50 initialises
# a TSS, L51 installs it into a runtime-built GDT.

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
for tok in 'Hello 64-bit!' 'multiheap ready' 'hello' 'Divide by zero error'; do
    echo "$chars" | grep -q "$tok" || { echo "FAIL: missing '$tok'"; ok=0; }
done

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
