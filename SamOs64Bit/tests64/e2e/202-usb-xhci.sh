#!/usr/bin/env bash
# e2e/202-usb-xhci.sh - qemu-xhci with a keyboard, a mouse and a
# stick, in each interrupt mode the driver has:
#   q35        MSI-X (the default; vector 92)
#   pc         MSI-X on i440FX
#   q35-msi    nec-usb-xhci,msix=off: plain MSI
#   q35-intx   samos.usb_intx: the INTx line through the IOAPIC
#   q35-poll   samos.usb_poll: no interrupt, the BSP's tick polls
# Each boot: the controller and the three devices enumerated and bound,
# PS/2 off (i8042=off) so `ls 1:/` is typed through the USB keyboard,
# and its output read off the USB stick. With an interrupt, marker 194
# counts stick transfers that ended at it and 187 (polled waits) stays
# unset; polled, 194 stays 0.
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_usb_lib.sh"

_xhci_mon() {
    if ! shell_boot; then
        printf 'quit\n'
        return
    fi
    shell_cmd 'ls 1:/'
    printf 'pmemsave 0x6000 %d "%s"\n' "$E2E_MARKER_REGION_SIZE" "$E2E_DUMP"
    sleep 1
    printf 'quit\n'
}

# xhci_boot <mode>
xhci_boot() {
    local mode=$1 opts="" machine=q35
    USB_HC=qemu-xhci
    E2E_IMAGE=$E2E_OS_IMG
    case "$mode" in
        pc)       machine=pc ;;
        q35-msi)  USB_HC=nec-usb-xhci; opts=",msix=off" ;;
        q35-intx) usb_cmdline samos.usb_intx ;;
        q35-poll) usb_cmdline samos.usb_poll ;;
    esac
    E2E_MACHINE=$machine
    usb_stick "$E2E_TMP/stick.img" USBSTICK
    usb_args "$opts" "$E2E_TMP/stick.img"
    E2E_EXTRA_QEMU_ARGS+=" -machine i8042=off"
    rm -f "$E2E_DUMP"
    e2e_run_qemu 300 _xhci_mon
    usb_enum_check "$mode" stick
    case "$mode" in
        q35|pc)   expect_serial '^xhci: interrupts by MSI-X, vector 92$' "$mode: MSI-X" ;;
        q35-msi)  expect_serial '^xhci: interrupts by MSI, vector 92$' "$mode: MSI" ;;
        q35-intx) expect_serial '^disk: xhci completions by interrupt, GSI [0-9]+ level active-high, vector 92$' "$mode: INTx through the IOAPIC" ;;
        q35-poll) expect_serial '^xhci: .*, running, polled$' "$mode: polled" ;;
    esac
    expect_serial "$SHELL_BANNER" "$mode: shell up"
    expect_serial '^usb-hid: first key event$' "$mode: keys came through the USB keyboard"
    expect_serial '^USBHELLO\.TXT +25$' "$mode: ls 1:/ listed the stick"
    [ -s "$E2E_DUMP" ] || e2e_fail "$mode: no marker dump"
    local irq keys
    irq=$(usb_count 194)
    keys=$(usb_count 195)
    e2e_log "$mode: stick transfers at the interrupt: $irq, USB key events: $keys"
    [ "$keys" -ge 14 ] || e2e_fail "$mode: only $keys USB key events"
    if [ "$mode" = q35-poll ]; then
        [ "$irq" -eq 0 ] || e2e_fail "$mode: $irq transfers at an interrupt, expected none"
    else
        [ "$irq" -ge 20 ] || e2e_fail "$mode: only $irq stick transfers at the interrupt"
        [ "$(usb_count 187)" -eq 0 ] || e2e_fail "$mode: $(usb_count 187) disk waits fell back to polling"
    fi
}

for mode in q35 pc q35-msi q35-intx q35-poll; do
    xhci_boot $mode
done
echo "PASS: e2e/202 xHCI with keyboard, mouse and stick: MSI-X (q35, pc), MSI, INTx, polled; typed and listed through USB only"
