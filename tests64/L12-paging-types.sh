#!/bin/bash
# tests64/L12-paging-types.sh
#
# Lecture 12 - introduce the 64-bit paging type vocabulary
# (paging_desc_entry / paging_pml_entries / paging_desc) AND grow
# PT_Table to cover ~40 MiB so the kheap range is reachable again.
#
# paging.c is still empty (mapping API lands at L13) so the only
# observable here is that the kernel still boots, prints, and the
# long-mode invariants hold under the new larger 4-KiB-page map.

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

dump=$(mktemp)
regs=$(mktemp)
trap 'rm -f "$dump" "$regs"' EXIT

(
    sleep 2
    printf 'pmemsave 0xb8000 4096 "%s"\n' "$dump"
    printf 'info registers\nquit\n'
) | timeout 12 qemu-system-x86_64 -hda bin/os.bin -m 256 -accel tcg \
    -display none -vga std -monitor stdio -no-reboot > "$regs" 2>&1

chars=$(od -An -v -tx1 -w1 "$dump" \
            | awk 'NR%2==1 {print $1}' \
            | xxd -r -p \
            | tr '\0' ' ')

ok=1
echo "$chars" | grep -q 'Hello 64-bit!' || { echo "FAIL: banner missing"; ok=0; }

cs=$(grep -oE 'CS =[0-9a-f]+' "$regs" | head -1 | cut -d= -f2)
cr0=$(grep -oE 'CR0=[0-9a-f]+' "$regs" | head -1 | cut -d= -f2)
cr4=$(grep -oE 'CR4=[0-9a-f]+' "$regs" | head -1 | cut -d= -f2)
efer=$(grep -oE 'EFER=[0-9a-f]+' "$regs" | head -1 | cut -d= -f2)

[ "$cs" = "0018" ] || { echo "FAIL: CS=$cs"; ok=0; }
top=$(printf '%d' "0x${cr0:0:1}"); [ $((top & 8)) -ne 0 ] || { echo "FAIL: CR0 PG"; ok=0; }
pae=$(printf '%d' "0x${cr4:(-2):2}"); [ $((pae & 0x20)) -ne 0 ] || { echo "FAIL: CR4 PAE"; ok=0; }
efer_dec=$((16#${efer##*([0])})); [ $((efer_dec & 0x400)) -ne 0 ] || { echo "FAIL: EFER LMA"; ok=0; }

[ $ok -eq 1 ] || exit 1
exit 0
