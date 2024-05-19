#!/usr/bin/env bash
# e2e/209-usb-dma32.sh - an xHCI without AC64 (32-bit DMA).
# QEMU's controllers all report AC64, so samos.usb_dma32 makes the
# driver treat it as 32-bit (xhci.c hc_dma_page):
#   1. q35 with 6 GiB, so RAM goes on above 4 GiB: every page the
#      controller is given (DCBAA, command and event rings, scratchpads,
#      contexts, transfer rings and buffers) is below 4 GiB; the
#      keyboard types `ls 1:/` (i8042=off) and the stick lists.
#   2. samos.usb_dma32=1, a 1 MiB limit no page meets: the controller
#      is refused cleanly ("controller not started") and the kernel
#      goes on to the shell, typed through PS/2.
# e2e-requires: xhci (qemu-xhci on PCI, x86 machines and OVMF; on aarch64 virt nothing assigns PCI BARs and there is no MSI)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_usb_lib.sh"

_dma32_mon() {
    if ! shell_boot; then
        printf 'quit\n'
        return
    fi
    shell_cmd 'ls 1:/'
    sleep 1
    printf 'quit\n'
}

E2E_MACHINE=q35
export E2E_MEM=6G
usb_cmdline samos.usb_dma32
usb_stick "$E2E_TMP/stick.img" USBSTICK
usb_args "" "$E2E_TMP/stick.img"
E2E_EXTRA_QEMU_ARGS+=" -machine i8042=off"
e2e_run_qemu 300 _dma32_mon
expect_serial '^xhci: 32-bit addressing, DMA below 4096 MiB$' "4 GiB: the controller is treated as 32-bit"
usb_enum_check "4 GiB" stick
expect_no_serial 'refused|controller not started' "4 GiB: no page refused"
expect_serial "$SHELL_BANNER" "4 GiB: shell up"
expect_serial '^usb-hid: first key event$' "4 GiB: keys came through the USB keyboard"
expect_serial '^USBHELLO\.TXT +25$' "4 GiB: ls 1:/ listed the stick"

export E2E_MEM=512M
usb_cmdline samos.usb_dma32=1
usb_args "" "$E2E_TMP/stick.img"
e2e_run_qemu 300 _dma32_mon
expect_serial '^xhci: 32-bit addressing, DMA below 1 MiB$' "1 MiB: the lower test limit"
expect_serial '^xhci: a DMA page above what the 32-bit controller reaches, refused$' "1 MiB: the page is refused"
expect_serial '^xhci: controller not started$' "1 MiB: the controller is not started"
expect_no_serial '^usb: port' "1 MiB: no device enumerated"
expect_serial "$SHELL_BANNER" "1 MiB: shell up without USB"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT|USER FAULT|spinlock:' "1 MiB: no fault"
echo "PASS: e2e/209 xHCI without AC64: DMA below 4 GiB with RAM above it, a page out of reach refused cleanly"
