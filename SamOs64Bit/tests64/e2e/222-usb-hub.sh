#!/usr/bin/env bash
# e2e/222-usb-hub.sh - devices behind USB hubs.
# q35 with i8042=off; qemu-xhci with a usb-hub (QEMU's is a full-speed
# USB 2 hub with 8 ports) on root port 1 and behind it:
#   port 1: usb-kbd       (route 0x1)
#   port 2: usb-storage   (route 0x2, full speed: 64-byte packets)
#   port 3: a second usb-hub (route 0x3), with usb-mouse on its port 1
#           (route 0x13: a hub behind a hub)
# Each hub is bound by usb_hub.c (8 ports, depth 0 and 1), every
# device behind them is enumerated with its route string and bound;
# the keyboard behind the hub types `ls 1:/` and `cat` of the stick
# behind the same hub, and monitor mouse_move moves the cursor of the
# mouse two hubs down. (QEMU has no high-speed or USB 3 hub, so the
# TT fields are not exercised here.)
# e2e-requires: xhci (qemu-xhci on PCI)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_usb_lib.sh"

stick="$E2E_TMP/stick.img"
usb_stick "$stick" USBSTICK

_hub_mon() {
    if ! shell_boot; then
        printf 'quit\n'
        return
    fi
    shell_cmd 'ls 1:/'
    shell_cmd 'cat 1:/USBHELLO.TXT'
    printf 'mouse_move 30 20\n'
    sleep 1
    e2e_pmemsave
    sleep 1
    printf 'quit\n'
}

E2E_MACHINE=q35
E2E_EXTRA_QEMU_ARGS="-device qemu-xhci,id=xhci -device usb-hub,bus=xhci.0,port=1,id=hub1"
E2E_EXTRA_QEMU_ARGS+=" -device usb-kbd,bus=xhci.0,port=1.1,id=ukbd"
E2E_EXTRA_QEMU_ARGS+=" -drive file=$stick,format=raw,if=none,id=stick -device usb-storage,bus=xhci.0,port=1.2,drive=stick,id=ustick"
E2E_EXTRA_QEMU_ARGS+=" -device usb-hub,bus=xhci.0,port=1.3,id=hub2 -device usb-mouse,bus=xhci.0,port=1.3.1,id=umouse"
usb_only_keyboard
e2e_run_qemu 300 _hub_mon

hex='[0-9a-f]{4}:[0-9a-f]{4}'
expect_serial '^xhci: 0:[0-9]+\.0 1b36:000d, [0-9]+ slots, 8 ports, USB 3: 1-4, USB 2: 5-8, ' "xHCI running"
expect_serial "^usb: port [5-8] \\(USB 2\\) full speed, slot [0-9]+, $hex QEMU USB Hub\$" "hub on a root port enumerated"
expect_serial '^usb-hub: slot [0-9]+, 8 ports, USB 2, depth 0$' "first hub bound"
expect_serial '^usb: hub slot [0-9]+ port 1 \(route 0x1\) full speed, slot [0-9]+, 0627:0001 QEMU USB Keyboard$' \
    "keyboard behind the hub enumerated with route 0x1"
expect_serial '^usb-hid: keyboard, slot [0-9]+, boot protocol, endpoint 1 IN, interval [0-9]+$' "keyboard bound"
expect_serial '^usb: hub slot [0-9]+ port 2 \(route 0x2\) full speed, slot [0-9]+, 46f4:0001 QEMU USB HARDDRIVE$' \
    "stick behind the hub enumerated with route 0x2"
expect_serial '^usb-msc: slot [0-9]+, bulk-only, endpoints [0-9]+ IN [0-9]+ OUT, 64-byte packets$' "stick bound at full speed"
expect_serial '^usb-msc: slot [0-9]+, QEMU QEMU HARDDISK, disk [0-9]+, 32 MiB$' "stick is a disk"
expect_serial "^usb: hub slot [0-9]+ port 3 \\(route 0x3\\) full speed, slot [0-9]+, $hex QEMU USB Hub\$" \
    "second hub behind the first enumerated"
expect_serial '^usb-hub: slot [0-9]+, 8 ports, USB 2, depth 1$' "second hub bound at depth 1"
expect_serial '^usb: hub slot [0-9]+ port 1 \(route 0x13\) full speed, slot [0-9]+, 0627:0001 QEMU USB Mouse$' \
    "mouse two hubs down enumerated with route 0x13"
expect_serial '^usb-hid: mouse, slot [0-9]+, boot protocol, endpoint 1 IN, interval [0-9]+$' "mouse bound"
expect_serial "$SHELL_BANNER" "shell up through the keyboard behind the hub"
expect_serial '^USBHELLO\.TXT' "ls 1:/ typed on the hub keyboard lists the stick behind the hub"
expect_serial '^hello from the usb stick$' "the stick behind the hub reads"
expect_serial '^usb-hid: cursor registered with the mouse layer$' "mouse two hubs down has a cursor"
expect_serial 'usb-hid: first mouse report, cursor at 670,420$' "mouse_move moved it by 30,20"
[ -s "$E2E_DUMP" ] || e2e_fail "no marker dump"
keys=$(usb_count 195)
e2e_log "USB key events: $keys"
[ "$keys" -ge 30 ] || e2e_fail "only $keys USB key events"
expect_no_serial 'no driver for slot|KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:|xhci: command|timed out|enumeration failed|did not enable|disk disabled' \
    "no unbound device, no fault, no USB error"
echo "PASS: e2e/222 keyboard, stick and a hub with a mouse behind a USB hub: enumerated, bound, typing, reading"
