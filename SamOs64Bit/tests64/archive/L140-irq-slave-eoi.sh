#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# no_interrupt_handler EOIs both PICs.
awk '/^void no_interrupt_handler\(\)/,/^}$/{print}' "$ROOT/src/idt/idt.c" > /tmp/noiq.$$
grep -q 'outb(0x20, 0x20)' /tmp/noiq.$$ || fail "no_interrupt_handler: master EOI missing"
grep -q 'outb(0xA0, 0x20)' /tmp/noiq.$$ || fail "no_interrupt_handler: slave EOI missing"
rm -f /tmp/noiq.$$

# interrupt_handler EOIs both PICs.
awk '/^void interrupt_handler\(/,/^}$/{print}' "$ROOT/src/idt/idt.c" > /tmp/iq.$$
grep -q 'outb(0x20, 0x20)' /tmp/iq.$$ || fail "interrupt_handler: master EOI missing"
grep -q 'outb(0xA0, 0x20)' /tmp/iq.$$ || fail "interrupt_handler: slave EOI missing"
# Save/restore commented out.
grep -q '// task_current_save_state' /tmp/iq.$$ || fail "task_current_save_state not commented"
grep -q '// task_page' /tmp/iq.$$ || fail "task_page not commented"
rm -f /tmp/iq.$$

# kernel_main calls enable_interrupts after window_create.
grep -q 'enable_interrupts()' "$ROOT/src/kernel.c" || fail "kernel.c: enable_interrupts missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L140 slave PIC EOI + interrupts on"
