#!/usr/bin/env bash
# e2e/202-usb-xhci.sh - qemu-xhci with a keyboard, a mouse and a
# stick, in each interrupt mode the driver has:
#   q35        MSI-X (the default; vector 92)
#   pc         MSI-X on i440FX
#   q35-msi    nec-usb-xhci,msix=off: plain MSI
#   q35-intx   samos.usb_intx: the INTx line through the IOAPIC
#   q35-poll   samos.usb_poll: no interrupt, the BSP's tick polls
# S-PAR - on aarch64 virt (-kernel, the kernel assigns the BARs):
#   virt       MSI-X through the GICv3 ITS (LPI 8204)
#   virt-msi   nec-usb-xhci,msix=off: MSI through the ITS
#   virt-intx  samos.usb_intx: INTx through the host's interrupt-map,
#              a GIC SPI
#   virt-noits virt,its=off: no ITS, so MSI is refused and INTx taken
#   virt-poll  samos.usb_poll
# Each boot: the controller and the three devices enumerated and bound,
# PS/2 off (i8042=off) so `ls 1:/` is typed through the USB keyboard,
# and its output read off the USB stick. With an interrupt, marker 194
# counts stick transfers that ended at it and 187 (polled waits) stays
# unset; polled, 194 stays 0.
# e2e-requires: xhci (qemu-xhci on PCI with MSI-X, MSI and INTx)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_usb_lib.sh"

_xhci_mon() {
    if ! shell_boot; then
        printf 'quit\n'
        return
    fi
    shell_cmd 'ls 1:/'
    e2e_pmemsave
    sleep 1
    printf 'quit\n'
}

# xhci_boot <mode>
xhci_boot() {
    local mode=$1 opts="" machine=q35 msi
    USB_HC=qemu-xhci
    E2E_IMAGE=$E2E_OS_IMG
    E2E_AARCH64_MACHINE_OPTS=
    case "$mode" in
        pc)         machine=pc ;;
        *-msi)      USB_HC=nec-usb-xhci; opts=",msix=off" ;;
        *-intx)     usb_cmdline samos.usb_intx ;;
        *-poll)     usb_cmdline samos.usb_poll ;;
        virt-noits) E2E_AARCH64_MACHINE_OPTS=",its=off" ;;
    esac
    E2E_MACHINE=$machine
    usb_stick "$E2E_TMP/stick.img" USBSTICK
    usb_args "$opts" "$E2E_TMP/stick.img"
    usb_only_keyboard
    msi=$(usb_msi_line)
    rm -f "$E2E_DUMP"
    e2e_run_qemu 300 _xhci_mon
    usb_enum_check "$mode" stick
    case "$mode" in
        q35|pc|virt) expect_serial "^xhci: interrupts by MSI-X, vector $msi\$" "$mode: MSI-X" ;;
        *-msi)    expect_serial "^xhci: interrupts by MSI, vector $msi\$" "$mode: MSI" ;;
        q35-intx) expect_serial '^disk: xhci completions by interrupt, GSI [0-9]+ level active-high, vector 92$' "$mode: INTx through the IOAPIC" ;;
        virt-intx|virt-noits)
                  expect_serial '^disk: xhci completions by interrupt, GSI 3[5-8] level active-high, vector 3[5-8]$' \
                      "$mode: INTx, a GIC SPI from the interrupt-map" ;;
        *-poll)   expect_serial '^xhci: .*, running, polled$' "$mode: polled" ;;
    esac
    if [ "$mode" = virt-noits ]; then
        expect_serial '^its: none in the device tree, PCI MSI off \(INTx\)$' "$mode: no ITS"
    fi
    expect_serial "$SHELL_BANNER" "$mode: shell up"
    expect_serial '^usb-hid: first key event$' "$mode: keys came through the USB keyboard"
    expect_serial '^USBHELLO\.TXT +25$' "$mode: ls 1:/ listed the stick"
    [ -s "$E2E_DUMP" ] || e2e_fail "$mode: no marker dump"
    local irq keys
    irq=$(usb_count 194)
    keys=$(usb_count 195)
    e2e_log "$mode: stick transfers at the interrupt: $irq, USB key events: $keys"
    [ "$keys" -ge 14 ] || e2e_fail "$mode: only $keys USB key events"
    if [ "${mode%-poll}" != "$mode" ]; then
        [ "$irq" -eq 0 ] || e2e_fail "$mode: $irq transfers at an interrupt, expected none"
    else
        [ "$irq" -ge 20 ] || e2e_fail "$mode: only $irq stick transfers at the interrupt"
        [ "$(usb_count 187)" -eq 0 ] || e2e_fail "$mode: $(usb_count 187) disk waits fell back to polling"
    fi
}

modes="q35 pc q35-msi q35-intx q35-poll"
[ "$E2E_ARCH" = aarch64 ] && modes="virt virt-msi virt-intx virt-noits virt-poll"
for mode in $modes; do
    xhci_boot $mode
done
echo "PASS: e2e/202 xHCI with keyboard, mouse and stick ($modes); typed and listed through USB only"
