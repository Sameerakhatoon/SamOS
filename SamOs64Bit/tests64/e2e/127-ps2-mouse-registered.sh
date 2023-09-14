#!/usr/bin/env bash
# e2e/127-ps2-mouse-registered.sh - kernel_main registers the
# real PS/2 mouse driver (mouse_system_load_static_drivers was an
# empty stub), its reset handshake succeeds against QEMU's i8042 aux
# device, and the cursor window is attached once the window system
# is up. Packets need IRQ12, which stays masked until the core
# session fixes the IRQ enum in idt/irq.h; this test does not cover
# movement.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 170 -eq 1 "PS/2 mouse registered with a cursor window"
echo "PASS: e2e/127 PS/2 mouse registered"
