#!/usr/bin/env bash
# e2e/116-user-keyboard-input.sh - L82-L88 PS/2 keyboard driver
# end to end:
#
#   QEMU sendkey  -> PIC IRQ1
#                 -> kernel keyboard_classic_handler
#                 -> per-process keyboard buffer
#                 -> samos_getkey() in blank.c
#                 -> samos_e2e_mark(USER_KEY_RECEIVED, 1)
#
# Status: EXPECTED FAIL. The IRQ1 wiring is present
# (kernel_main calls keyboard_init() which inserts
# classic_init() which IRQ_enable(IRQ_KEYBOARD)) and the
# user-side samos_getkey trampoline reaches the kernel, but
# the QEMU monitor sendkey scancodes are not currently
# landing in the per-process keyboard buffer under headless
# UEFI boot. The leading suspects are:
#   * sendkey scancode set vs classic driver scancode table
#   * IRQ1 mask state at the moment sendkey runs (the kernel
#     may have left IRQ1 masked at the PIC depending on init
#     ordering between PIC remap and keyboard_init)
#
# This script is kept in the suite so a future fix lights up
# the assertion automatically; today run-all will report it
# as a known failure and continue.

set -uo pipefail
. "$(dirname "$0")/_lib.sh"

e2e_select_image

qemu_args=(
    -m 512M
    -accel tcg
    -display none
    -monitor stdio
    -no-reboot
    -drive "file=$E2E_IMAGE,format=raw,if=ide"
    -bios "$E2E_OVMF"
    -machine pc
    -cpu Skylake-Server
)

rm -f "$E2E_DUMP"

# Boot for ~12 s (kernel boot + selftest + user-task startup),
# then fire sendkey at intervals to give the kernel multiple
# opportunities to receive an IRQ1. We pmemsave at 28 s which
# is past both the host-side sendkey timing AND the user-side
# polling loop's 15-second budget.
(
    sleep 12
    printf 'sendkey a\n'
    sleep 1
    printf 'sendkey b\n'
    sleep 1
    printf 'sendkey c\n'
    sleep 1
    printf 'sendkey d\n'
    sleep 1
    printf 'sendkey ret\n'
    sleep 12
    printf 'pmemsave 0x6000 %d "%s"\n' "$E2E_MARKER_REGION_SIZE" "$E2E_DUMP"
    printf 'quit\n'
) | timeout 45 qemu-system-x86_64 "${qemu_args[@]}" \
        > /tmp/samos-e2e-qemu.log 2>&1 || true

[ -s "$E2E_DUMP" ] || e2e_fail "no marker dump (qemu log /tmp/samos-e2e-qemu.log)"

expect_stage_value 160 -eq 1 "blank.c received a key via samos_getkey"
echo "PASS: e2e/116 user keyboard input (sendkey -> ring 3)"
