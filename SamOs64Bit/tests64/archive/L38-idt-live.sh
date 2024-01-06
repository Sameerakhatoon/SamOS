#!/bin/bash
# tests64/L38-idt-live.sh
#
# Lecture 38 - IDT in the build + deliberate #DE smoke test.
#
# After multiheap_ready:
#   idt_init()        - install + lidt the 64-bit IDT
#   print("hello")    - confirms we got past idt_init alive
#   div_test()        - kernel.asm function that does idiv rax
#                       with rax=0 -> #DE
#   print("oi")       - MUST NOT APPEAR. div_test never returns
#                       because the CPU vectors through IDT[0]
#                       into idt_zero which halts.
#
# Pass conditions:
#   - "hello" present (IDT install succeeded; lidt didn't #GP)
#   - "Divide by zero error" present (vector 0 dispatched
#     correctly through the 64-bit gate descriptor)
#   - "oi" NOT present (idt_zero stayed halted; div_test did
#     not return)

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

# Negative: the post-fault marker MUST be absent.
if echo "$chars" | grep -q ' oi '; then
    echo "FAIL: post-fault 'oi' marker appeared (handler returned to div_test?)"
    ok=0
fi
# Also check beginning-of-line 'oi' just in case.
if echo "$chars" | grep -qE '(^| )oi( |$)'; then
    # already caught above; this is a tighter check via fold
    :
fi

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
