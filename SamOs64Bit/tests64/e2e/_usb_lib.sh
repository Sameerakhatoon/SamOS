#!/usr/bin/env bash
# Helpers for the USB e2e tests (202-205). Sourced after _lib.sh
# and _shell_lib.sh. Not a test itself.
#
# QEMU's qemu-xhci (1b36:000d) or nec-usb-xhci (1033:0194), each with
# 4 USB 3 and 4 USB 2 ports, and usb-kbd, usb-mouse and usb-storage
# on it. Monitor `sendkey` and
# `mouse_move` go to the active input handler; with `i8042=off` there
# is no PS/2 controller at all, so every key the shell gets came
# through the USB keyboard.

# usb_stick <image> <label>: a 32 MiB FAT16 image with USBHELLO.TXT.
usb_stick() {
    local img=$1 label=$2
    rm -f "$img"
    mkfs.fat -F 16 -n "$label" -C "$img" 32768 >/dev/null || e2e_fail "mkfs.fat $img"
    printf 'hello from the usb stick\n' > "$E2E_TMP/usbhello.txt"
    MTOOLS_SKIP_CHECK=1 mcopy -i "$img" "$E2E_TMP/usbhello.txt" ::/USBHELLO.TXT || e2e_fail "mcopy USBHELLO.TXT"
}

# usb_cmdline <text>: boot a copy of the image with CMDLINE.TXT.
usb_cmdline() {
    cp "$E2E_OS_IMG" "$E2E_TMP/os.img"
    E2E_IMAGE="$E2E_TMP/os.img"
    printf '%s\n' "$1" > "$E2E_TMP/CMDLINE.TXT"
    MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
        || e2e_fail "mcopy CMDLINE.TXT"
}

# usb_args [xhci options] [stick image]: the controller (USB_HC, default
# qemu-xhci; nec-usb-xhci has msi and msix switches), a keyboard, a
# mouse and, with an image, a stick, into E2E_EXTRA_QEMU_ARGS.
usb_args() {
    local opts=${1:-} img=${2:-}
    E2E_EXTRA_QEMU_ARGS="-device ${USB_HC:-qemu-xhci},id=xhci$opts -device usb-kbd,bus=xhci.0,id=ukbd -device usb-mouse,bus=xhci.0,id=umouse"
    if [ -n "$img" ]; then
        E2E_EXTRA_QEMU_ARGS+=" -drive file=$img,format=raw,if=none,id=stick -device usb-storage,bus=xhci.0,drive=stick,id=ustick"
    fi
}

# S-PAR - usb_only_keyboard: no keyboard but the USB one, so every key
# the shell gets came through it (x86: i8042=off; aarch64 and riscv64
# virt: no virtio keyboard, E2E_<ARCH>_KBD=none).
usb_only_keyboard() {
    case "$E2E_ARCH" in
        aarch64) E2E_AARCH64_KBD=none ;;
        riscv64) E2E_RISCV64_KBD=none ;;
        *)       E2E_EXTRA_QEMU_ARGS+=" -machine i8042=off" ;;
    esac
}

# S-PAR - usb_msi_line: the line xhci reports for MSI and MSI-X (x86:
# vector 92; aarch64: LPI 8204 through the GICv3 ITS; riscv64: IMSIC
# EIID 140).
usb_msi_line() {
    case "$E2E_ARCH" in
        aarch64) echo 8204 ;;
        riscv64) echo 140 ;;
        *)       echo 92 ;;
    esac
}

# Parity (riscv64) - MSI needs the IMSIC: the USB tests run on
# virt,aia=aplic-imsic unless a test picks the machine (e2e/202's
# virt-plic mode).
if [ "$E2E_ARCH" = riscv64 ]; then
    E2E_RISCV64_MACHINE_OPTS=${E2E_RISCV64_MACHINE_OPTS-,aia=aplic-imsic}
fi

# usb_wait_count <ERE> <n> <seconds>: (monitor script) wait until the
# serial log has <n> lines matching.
usb_wait_count() {
    local re=$1 n=$2 limit=$3 t=0
    while [ $t -lt $((limit * 10)) ]; do
        [ "$(tr -d '\r' < "$E2E_SERIAL" 2>/dev/null | grep -Ec -- "$re")" -ge "$n" ] && return 0
        sleep 0.1
        t=$((t + 1))
    done
    return 1
}

# usb_count <marker>: a running-count marker, 0 when never set.
usb_count() {
    if [ "$(marker_high "$1")" = "$(expected_high)" ]; then marker_value "$1"; else echo 0; fi
}

# usb_enum_check <tag> [stick]: the three devices were enumerated and bound.
usb_enum_check() {
    local tag=$1 stick=${2:-}
    expect_serial '^xhci: 0:[0-9]+\.0 (1b36:000d|1033:0194), [0-9]+ slots, 8 ports, USB 3: 1-4, USB 2: 5-8, (32|64)-byte contexts, [0-9]+ scratchpads, running' \
        "$tag: xHCI found and running"
    expect_serial '^usb: port [5-8] \(USB 2\) (full|high) speed, slot [0-9]+, 0627:0001 QEMU USB Keyboard$' "$tag: keyboard enumerated"
    expect_serial '^usb-hid: keyboard, slot [0-9]+, boot protocol, endpoint 1 IN, interval [0-9]+$' "$tag: keyboard bound"
    expect_serial '^usb: port [5-8] \(USB 2\) (full|high) speed, slot [0-9]+, 0627:0001 QEMU USB Mouse$' "$tag: mouse enumerated"
    expect_serial '^usb-hid: mouse, slot [0-9]+, boot protocol, endpoint 1 IN, interval [0-9]+$' "$tag: mouse bound"
    if [ -n "$stick" ]; then
        expect_serial '^usb: port [1-4] \(USB 3\) super speed, slot [0-9]+, 46f4:0001 QEMU USB HARDDRIVE$' "$tag: stick enumerated on a USB 3 port"
        expect_serial '^usb-msc: slot [0-9]+, bulk-only, endpoints [0-9]+ IN [0-9]+ OUT, 1024-byte packets$' "$tag: stick bound"
        expect_serial '^usb-msc: slot [0-9]+, QEMU QEMU HARDDISK, disk [0-9]+, 32 MiB$' "$tag: stick is a disk"
    fi
    expect_no_serial 'KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:|xhci: command|timed out|enumeration failed|did not enable|disk disabled' "$tag: no fault, no USB error"
}
