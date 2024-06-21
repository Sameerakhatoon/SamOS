#!/usr/bin/env bash
# e2e/211-usb-xhci-mmio.sh - the xHCI driver's platform path, the
# one a device-tree xHCI takes (riscv64's generic-xhci or a Cadence
# USB3 host): no PCI configuration access after discovery, registers
# from an address, the interrupt a GSI. No QEMU machine of the ports
# has a platform xHCI, so samos.usb_as_mmio hands q35's qemu-xhci to
# that path (BAR 0, its INTx GSI). A keyboard, a mouse and a stick:
# enumerated and bound, `ls 1:/` typed through the USB keyboard (PS/2
# off) lists the stick, its transfers end at the interrupt.
# e2e-requires: xhci (qemu-xhci on PCI, x86 machines and OVMF; on aarch64 virt nothing assigns PCI BARs and there is no MSI)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_usb_lib.sh"

_mmio_mon() {
    if ! shell_boot; then
        printf 'quit\n'
        return
    fi
    shell_cmd 'ls 1:/'
    printf 'pmemsave 0x6000 %d "%s"\n' "$E2E_MARKER_REGION_SIZE" "$E2E_DUMP"
    sleep 1
    printf 'quit\n'
}

E2E_MACHINE=q35
usb_cmdline samos.usb_as_mmio
usb_stick "$E2E_TMP/stick.img" USBSTICK
usb_args "" "$E2E_TMP/stick.img"
E2E_EXTRA_QEMU_ARGS+=" -machine i8042=off"
rm -f "$E2E_DUMP"
e2e_run_qemu 300 _mmio_mon

expect_serial '^xhci: mmio 0x[0-9a-f]{16}, [0-9]+ slots, 8 ports, USB 3: 1-4, USB 2: 5-8, (32|64)-byte contexts, [0-9]+ scratchpads, running$' \
    "the controller started as a platform xHCI, with an interrupt"
expect_serial '^disk: xhci completions by interrupt, GSI [0-9]+ level active-(high|low), vector 92$' "its line is a GSI"
expect_no_serial '^xhci: interrupts by MSI' "no MSI on the platform path"
expect_serial '^usb: port [5-8] \(USB 2\) (full|high) speed, slot [0-9]+, 0627:0001 QEMU USB Keyboard$' "keyboard enumerated"
expect_serial '^usb-hid: keyboard, slot [0-9]+, boot protocol, endpoint 1 IN, interval [0-9]+$' "keyboard bound"
expect_serial '^usb: port [5-8] \(USB 2\) (full|high) speed, slot [0-9]+, 0627:0001 QEMU USB Mouse$' "mouse enumerated"
expect_serial '^usb: port [1-4] \(USB 3\) super speed, slot [0-9]+, 46f4:0001 QEMU USB HARDDRIVE$' "stick enumerated on a USB 3 port"
expect_serial '^usb-msc: slot [0-9]+, QEMU QEMU HARDDISK, disk [0-9]+, 32 MiB$' "stick is a disk"
expect_serial "$SHELL_BANNER" "shell up"
expect_serial '^usb-hid: first key event$' "keys came through the USB keyboard"
expect_serial '^USBHELLO\.TXT +25$' "ls 1:/ listed the stick"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:|xhci: command|timed out|enumeration failed|did not enable|disk disabled' \
    "no fault, no USB error"
[ -s "$E2E_DUMP" ] || e2e_fail "no marker dump"
irq=$(usb_count 194)
e2e_log "stick transfers at the interrupt: $irq"
[ "$irq" -ge 20 ] || e2e_fail "only $irq stick transfers at the interrupt"
echo "PASS: e2e/211 xHCI as a platform controller (samos.usb_as_mmio): GSI interrupt, keyboard, mouse, stick, typed and listed"
