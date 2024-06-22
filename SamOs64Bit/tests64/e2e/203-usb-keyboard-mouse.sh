#!/usr/bin/env bash
# e2e/203-usb-keyboard-mouse.sh - the USB HID boot keyboard and
# mouse feeding the keyboard and mouse layers.
#   1. q35 with i8042=off (no PS/2 at all): Shift, ':' and '_' typed
#      through usb-kbd reach the shell as from PS/2; Caps Lock on, a
#      word, Caps Lock off gives the word in capitals (the modifier and
#      Caps Lock state are the PS/2 translator's); marker 195 counts
#      the key events. The USB mouse registers a cursor of its own and
#      monitor `mouse_move` moves it.
#   2. pc (PS/2 mouse present): the USB mouse drives the PS/2 cursor.
# S-PAR - on aarch64 virt part 1 only, without the virtio keyboard;
# parity: on riscv64 virt (aia=aplic-imsic, _usb_lib.sh) too.
# e2e-requires: xhci (qemu-xhci on PCI)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_usb_lib.sh"

_hid_mon() {
    if ! shell_boot; then
        printf 'quit\n'
        return
    fi
    shell_cmd 'echo Hello: USB_Keys'
    shell_type 'echo '
    printf 'sendkey caps_lock\n'
    sleep 0.3
    shell_type 'caps'
    printf 'sendkey caps_lock\n'
    sleep 0.3
    shell_cmd ' done'
    printf 'mouse_move 30 20\n'
    sleep 1
    printf 'mouse_button 1\n'
    sleep 0.3
    printf 'mouse_button 0\n'
    sleep 1
    e2e_pmemsave
    sleep 1
    printf 'quit\n'
}

E2E_MACHINE=q35
usb_args ""
usb_only_keyboard
e2e_run_qemu 300 _hid_mon
usb_enum_check "q35"
expect_serial "$SHELL_BANNER" "q35: shell up"
expect_serial '^Hello: USB_Keys$' "q35: Shift, ':' and '_' through usb-kbd"
expect_serial '^CAPS done$' "q35: Caps Lock through usb-kbd"
expect_serial '^usb-hid: cursor registered with the mouse layer$' "q35: USB mouse has its own cursor"
# The cursor starts at the centre of the screen (x86 1280x800,
# the ports' ramfb 1024x768), and mouse_move 30 20 is one report of +30,+20.
cursor=670,420
[ "$E2E_ARCH" = x86_64 ] || cursor=542,404
expect_serial "usb-hid: first mouse report, cursor at $cursor\$" "q35: mouse_move moved the cursor by 30,20 from the centre"
[ -s "$E2E_DUMP" ] || e2e_fail "q35: no marker dump"
keys=$(usb_count 195)
e2e_log "q35: USB key events: $keys"
[ "$keys" -ge 60 ] || e2e_fail "q35: only $keys USB key events"
if [ "$E2E_ARCH" != x86_64 ]; then
    echo "PASS: e2e/203 USB keyboard (Shift, Caps Lock) and mouse through the keyboard and mouse layers ($E2E_ARCH virt)"
    exit 0
fi

_ps2_mon() {
    e2e_wait_serial '^e2e: user selftest done' 120 || true
    sleep 1
    printf 'quit\n'
}
E2E_MACHINE=pc
usb_args ""
e2e_run_qemu 300 _ps2_mon
usb_enum_check "pc"
expect_serial '^usb-hid: mice move the PS/2 cursor$' "pc: USB mouse drives the PS/2 cursor"
echo "PASS: e2e/203 USB keyboard (Shift, Caps Lock) and mouse through the keyboard and mouse layers, no PS/2"
