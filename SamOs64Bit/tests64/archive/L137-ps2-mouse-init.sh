#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

[ -f "$ROOT/src/mouse/ps2mouse.h" ] || fail "ps2mouse.h missing"
[ -f "$ROOT/src/mouse/ps2mouse.c" ] || fail "ps2mouse.c missing"

grep -q 'ETIMEOUT' "$ROOT/src/status.h" || fail "status.h: ETIMEOUT missing"

for sym in PS2_COMMUNICATION_PORT PS2_STATUS_PORT \
           PS2_COMMAND_ENABLE_SECOND_PORT PS2_WRITE_TO_MOUSE \
           PS2_READ_CONFIG_BYTE PS2_UPDATE_CONFIG_BYTE \
           PS2_MOUSE_ENABLE_PACKET_STREAMING PS2_WAIT_FOR_INPUT_TO_CLEAR \
           PS2_WAIT_FOR_OUTPUT_TO_BE_SET PS2_MOUSE_RESET PS2_MOUSE_ACK \
           PS2_MOUSE_SELF_TEST_PASS ISR_MOUSE_INTERRUPT \
           PS2_STANDARD_MOUSE_DEVICE_ID PS2_SCROLL_WHEEL_MOUSE_DEVICE_ID; do
    grep -q "$sym" "$ROOT/src/mouse/ps2mouse.h" || fail "ps2mouse.h: $sym missing"
done
grep -q 'struct ps2_mouse {' "$ROOT/src/mouse/ps2mouse.h" || fail "struct missing"

for sym in ps2_mouse ps2_mouse_private ps2_mouse_wait ps2_mouse_write \
           ps2_mouse_init ps2_mouse_get; do
    grep -q "$sym" "$ROOT/src/mouse/ps2mouse.c" || fail "ps2mouse.c: $sym missing"
done

grep -q 'idt_register_interrupt_callback(ISR_MOUSE_INTERRUPT' "$ROOT/src/mouse/ps2mouse.c" \
    || fail "irq callback register missing"
grep -q 'IRQ_enable(IRQ_PS2_MOUSE)' "$ROOT/src/mouse/ps2mouse.c" \
    || fail "IRQ_enable call missing"

# Source-only.
if grep -q 'build/mouse/ps2mouse.o' "$ROOT/Makefile"; then
    fail "Makefile: ps2mouse.o should not yet be in FILES at L137"
fi

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L137 PS/2 mouse init"
