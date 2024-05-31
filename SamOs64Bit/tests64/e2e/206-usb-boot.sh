#!/usr/bin/env bash
# e2e/206-usb-boot.sh - in QEMU: boot from a USB stick on xHCI,
# as a PC without PS/2 or IDE would. A copy of bin/os.img is the only
# disk, on usb-storage (OVMF loads the kernel from it through its own
# xHCI driver, then SamOS resets the controller and enumerates again);
# q35 with i8042=off, a USB keyboard. Expect:
#   - @: (the SAMOS volume) is a partition of the USB disk, the user
#     programs load from it and the shell comes up;
#   - `ls @:/` typed through the USB keyboard lists SHELL.ELF;
#   - samos.bootlog wrote the boot's output to @:/BOOTLOG.TXT on the
#     stick, which Linux reads back with mtools (logging without
#     serial).
# e2e-requires: xhci uefi (OVMF loads the kernel from the stick through its own xHCI driver; the aarch64 and riscv64 ports boot with -kernel)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_usb_lib.sh"

usb_cmdline samos.bootlog
_boot_mon() {
    if ! shell_boot; then
        printf 'quit\n'
        return
    fi
    shell_cmd 'ls @:/'
    printf 'quit\n'
}
e2e_select_image
_boot_mon | timeout 400 qemu-system-x86_64 -machine q35,i8042=off -cpu Skylake-Server -m 512M \
    -bios "$E2E_OVMF" -display none -monitor stdio -serial "file:$E2E_SERIAL" -no-reboot \
    -device qemu-xhci,id=xhci \
    -drive "file=$E2E_IMAGE,format=raw,if=none,id=boot" -device usb-storage,bus=xhci.0,drive=boot,bootindex=0 \
    -device usb-kbd,bus=xhci.0 > "$E2E_QEMU_LOG" 2>&1 || true

expect_serial '^usb: port [1-4] \(USB 3\) super speed, slot [0-9]+, 46f4:0001 QEMU USB HARDDRIVE$' "boot stick enumerated"
expect_serial '^usb-msc: slot [0-9]+, QEMU QEMU HARDDISK, disk 0, [0-9]+ MiB$' "boot stick is disk 0"
expect_serial '^disk [0-9]+ part USB-MSC fs=FAT label=SAMOS ' "@: is on the stick"
expect_serial '^bootlog: @:/BOOTLOG\.TXT written$' "boot log written"
expect_serial "$SHELL_BANNER" "shell up"
expect_serial '^usb-hid: first key event$' "keys through the USB keyboard"
expect_serial '^SHELL\.ELF +[0-9]+$' "ls @:/ typed and listed"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:|xhci: command|timed out|disk disabled' "no fault, no USB error"

p2=$(( $(stat -c %s "$E2E_IMAGE") / 512 / 2 ))
MTOOLS_SKIP_CHECK=1 mtype -i "$E2E_IMAGE@@$((p2 * 512))" ::/BOOTLOG.TXT > "$E2E_TMP/bootlog.txt" 2>/dev/null \
    || e2e_fail "host: no BOOTLOG.TXT on the stick's SAMOS volume"
grep -q '^xhci: 0:[0-9]*\.0 1b36:000d' "$E2E_TMP/bootlog.txt" || e2e_fail "host: BOOTLOG.TXT lacks the xHCI line"
grep -q '^usb-msc: slot [0-9]*, QEMU QEMU HARDDISK, disk 0' "$E2E_TMP/bootlog.txt" || e2e_fail "host: BOOTLOG.TXT lacks the stick line"
e2e_log "host: BOOTLOG.TXT on the stick, $(wc -l < "$E2E_TMP/bootlog.txt") lines"
echo "PASS: e2e/206 booted from a USB stick on xHCI, typed through USB, boot log written to the stick"
