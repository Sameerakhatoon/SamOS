#!/usr/bin/env bash
# e2e/205-usb-hotplug.sh - plugging and unplugging at the shell,
# q35 with i8042=off, through the monitor's device_del / device_add.
#   - The keyboard pulled out: the port's disconnect is seen, the slot
#     freed; a new keyboard plugged in is enumerated, bound, and types.
#   - The stick pulled out: its disk fails every request from then on
#     (cat reports an error, nothing hangs or faults); a stick plugged
#     in after boot is enumerated and bound but not mounted (no
#     hot-plug mount yet).
# e2e-requires: xhci (qemu-xhci on PCI)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_usb_lib.sh"

stick="$E2E_TMP/stick.img"
stick2="$E2E_TMP/stick2.img"
usb_stick "$stick" USBSTICK
usb_stick "$stick2" LATER

_plug_mon() {
    if ! shell_boot; then
        printf 'quit\n'
        return
    fi
    shell_cmd 'echo before'
    printf 'device_del ukbd\n'
    e2e_wait_serial 'usb: port [5-8] disconnected, slot ' 30
    printf 'device_add usb-kbd,bus=xhci.0,id=ukbd2\n'
    usb_wait_count 'usb-hid: keyboard, slot ' 2 30
    sleep 2
    shell_cmd 'echo after replug'
    printf 'device_del ustick\n'
    e2e_wait_serial 'usb-msc: disk 1 gone' 30
    shell_cmd 'cat 1:/USBHELLO.TXT'
    shell_cmd 'echo still typing'
    printf 'drive_add 0 file=%s,format=raw,if=none,id=stick2\n' "$stick2"
    sleep 0.5
    printf 'device_add usb-storage,bus=xhci.0,drive=stick2,id=ustick2\n'
    e2e_wait_serial 'plugged in after boot: not mounted$' 30
    shell_cmd 'echo done'
    printf 'quit\n'
}
E2E_MACHINE=q35
usb_args "" "$stick"
usb_only_keyboard
e2e_run_qemu 400 _plug_mon
usb_enum_check "boot" stick
expect_serial '^before$' "typed before the unplug"
expect_serial 'usb: port [5-8] disconnected, slot [0-9]+$' "keyboard unplug seen"
[ "$(tr -d '\r' < "$E2E_SERIAL" | grep -Ec 'usb-hid: keyboard, slot')" -eq 2 ] || e2e_fail "the new keyboard was not bound"
expect_serial '^after replug$' "the new keyboard types"
expect_serial 'usb: port [1-4] disconnected, slot [0-9]+$' "stick unplug seen"
expect_serial 'usb-msc: disk 1 gone, its requests fail from now on$' "stick's disk failed"
expect_serial '^cat: 1:/USBHELLO.TXT: read error$' "reading the gone stick fails"
expect_serial '^still typing$' "the shell lives on after the stick went"
expect_serial 'usb-msc: slot [0-9]+, bulk-only, endpoints [0-9]+ IN [0-9]+ OUT, [0-9]+-byte packets, plugged in after boot: not mounted$' \
    "late stick bound, not mounted"
expect_serial '^done$' "shell still up at the end"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:' "no fault"
echo "PASS: e2e/205 USB keyboard and stick unplugged and plugged at the shell"
