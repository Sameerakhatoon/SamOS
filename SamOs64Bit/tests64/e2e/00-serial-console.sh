#!/usr/bin/env bash
# e2e/00-serial-console.sh - the kernel mirrors its console
# to COM1. Boot, then check the serial log for the first line
# kernel_main prints and for a line from late in the boot.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 20
expect_serial '^SamOS: kernel_main$' "kernel_main banner on COM1"
expect_serial 'register isr80h' "late-boot print() mirrored to COM1"
expect_no_serial 'KERNEL PANIC' "no panic during boot"
echo "PASS: e2e/00 serial console"
