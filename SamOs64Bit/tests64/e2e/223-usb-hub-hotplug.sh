#!/usr/bin/env bash
# e2e/223-usb-hub-hotplug.sh - plugging and unplugging behind a
# USB hub at the shell, q35 with i8042=off, through the monitor's
# device_del / device_add. Behind usb-hub on root port 1: usb-kbd on
# port 1, a stick with an MBR and one FAT16 partition on port 2, a
# second usb-hub on port 3 with usb-mouse behind it. Every change comes
# through the hub's status change endpoint (no root port changes).
#   - The keyboard pulled out of the hub: the hub port's disconnect is
#     seen, the slot freed; a new keyboard plugged into hub port 4 is
#     enumerated (route 0x4), bound and types.
#   - The second hub pulled out: it and the mouse behind it go.
#   - The stick pulled out: its disk and its partition are marked gone
#     (hot-plug removal): cat and ls of the partition say "no such
#     file or directory" at once, the shell types on.
# e2e-requires: xhci (qemu-xhci on PCI)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_usb_lib.sh"

# A 40 MiB stick: MBR, one FAT16 partition from 1 MiB.
stick="$E2E_TMP/stick.img"
rm -f "$stick"
truncate -s 40M "$stick"
echo 'start=2048, type=6' | sfdisk -q "$stick" >/dev/null || e2e_fail "sfdisk $stick"
mkfs.fat -F 16 -n PARTED --offset 2048 "$stick" $((39 * 1024)) >/dev/null || e2e_fail "mkfs.fat $stick"
printf 'hello from the usb stick\n' > "$E2E_TMP/usbhello.txt"
MTOOLS_SKIP_CHECK=1 mcopy -i "$stick@@$((2048 * 512))" "$E2E_TMP/usbhello.txt" ::/USBHELLO.TXT || e2e_fail "mcopy USBHELLO.TXT"

# Disk ids: 0 the boot disk, 1 the stick, 2 and 3 the boot disk's GPT
# partitions, 4 the stick's partition (REAL disks first, then the scan).
part=4

_hubplug_mon() {
    if ! shell_boot; then
        printf 'quit\n'
        return
    fi
    shell_cmd 'echo before'
    shell_cmd "cat $part:/USBHELLO.TXT"
    printf 'device_del ukbd\n'
    e2e_wait_serial 'usb: hub slot [0-9]+ port 1 disconnected, slot ' 30
    printf 'device_add usb-kbd,bus=xhci.0,port=1.4,id=ukbd2\n'
    usb_wait_count 'usb-hid: keyboard, slot ' 2 30
    sleep 2
    shell_cmd 'echo after replug'
    printf 'device_del hub2\n'
    e2e_wait_serial 'usb: hub slot [0-9]+ port 3 disconnected, slot ' 30
    sleep 1
    printf 'device_del ustick\n'
    e2e_wait_serial 'usb-msc: disk 1 gone' 30
    shell_cmd "cat $part:/USBHELLO.TXT"
    shell_cmd "ls $part:/"
    shell_cmd 'echo still typing'
    printf 'quit\n'
}

E2E_MACHINE=q35
E2E_EXTRA_QEMU_ARGS="-device qemu-xhci,id=xhci -device usb-hub,bus=xhci.0,port=1,id=hub1"
E2E_EXTRA_QEMU_ARGS+=" -device usb-kbd,bus=xhci.0,port=1.1,id=ukbd"
E2E_EXTRA_QEMU_ARGS+=" -drive file=$stick,format=raw,if=none,id=stick -device usb-storage,bus=xhci.0,port=1.2,drive=stick,id=ustick"
E2E_EXTRA_QEMU_ARGS+=" -device usb-hub,bus=xhci.0,port=1.3,id=hub2 -device usb-mouse,bus=xhci.0,port=1.3.1,id=umouse"
usb_only_keyboard
e2e_run_qemu 600 _hubplug_mon

expect_serial '^usb-hub: slot [0-9]+, 8 ports, USB 2, depth 0$' "hub bound"
expect_serial '^usb-msc: slot [0-9]+, QEMU QEMU HARDDISK, disk 1, 40 MiB$' "stick behind the hub is disk 1"
expect_serial '^before$' "typed on the keyboard behind the hub"
expect_serial '^hello from the usb stick$' "the stick's partition reads"
expect_serial 'usb: hub slot [0-9]+ port 1 disconnected, slot [0-9]+$' "keyboard unplugged from the hub"
expect_serial '^usb: hub slot [0-9]+ port 4 \(route 0x4\) full speed, slot [0-9]+, 0627:0001 QEMU USB Keyboard$' \
    "new keyboard on hub port 4 enumerated"
[ "$(tr -d '\r' < "$E2E_SERIAL" | grep -Ec 'usb-hid: keyboard, slot')" -eq 2 ] || e2e_fail "the new keyboard was not bound"
expect_serial '^after replug$' "the new keyboard types"
expect_serial 'usb: hub slot [0-9]+ port 3 disconnected, slot [0-9]+$' "second hub unplugged"
[ "$(tr -d '\r' < "$E2E_SERIAL" | grep -Ec 'usb: hub slot [0-9]+ port 1 disconnected')" -eq 2 ] \
    || e2e_fail "the mouse behind the second hub was not detached with it"
expect_serial 'usb: hub slot [0-9]+ port 2 disconnected, slot [0-9]+$' "stick unplugged from the hub"
expect_serial 'usb-msc: disk 1 gone, marked removed with 1 partition$' "the stick and its partition marked gone"
expect_serial "^cat: $part:/USBHELLO.TXT: no such file or directory\$" "cat of the gone partition: no such file"
expect_serial "^ls: $part:/: no such file or directory\$" "ls of the gone partition: no such file"
expect_serial '^still typing$' "the shell lives on"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:|xhci: command .* timed out|enumeration failed|no driver for slot' \
    "no fault, no USB error"
echo "PASS: e2e/223 keyboard, hub and stick unplugged and plugged behind a USB hub; the gone stick is marked removed"
