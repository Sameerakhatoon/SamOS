#!/bin/bash
# tests64/L10-kheap-online.sh
#
# Lecture 10 - restore the kheap so kmalloc / kfree work.
# kernel_main does:
#   terminal_initialize();
#   print("Hello 64-bit!\n");
#   kheap_init();
#   char* data = kmalloc(50); data = "ABC"; print(data);
#
# Asserts both "Hello 64-bit!" AND "ABC" land in VGA. If
# kheap_init crashes or kmalloc returns NULL the second print
# fails and the second token is missing.

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

dump=$(mktemp)
trap 'rm -f "$dump"' EXIT

( sleep 2; printf 'pmemsave 0xb8000 4096 "%s"\nquit\n' "$dump" ) | timeout 12 \
    qemu-system-x86_64 -hda bin/os.bin -m 256 -accel tcg -display none -vga std \
        -monitor stdio -no-reboot > /dev/null 2>&1

chars=$(od -An -v -tx1 -w1 "$dump" \
            | awk 'NR%2==1 {print $1}' \
            | xxd -r -p \
            | tr '\0' ' ')

ok=1
echo "$chars" | grep -q 'Hello 64-bit!' || { echo "FAIL: banner missing"; ok=0; }
echo "$chars" | grep -q 'ABC' || { echo "FAIL: kmalloc'd 'ABC' missing"; ok=0; }

if [ $ok -ne 1 ]; then
    echo "      first 240 chars: $(echo "$chars" | head -c 240)"
    exit 1
fi
exit 0
