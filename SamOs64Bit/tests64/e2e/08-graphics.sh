#!/usr/bin/env bash
# e2e/08-graphics.sh - graphics_setup_stage_two completed; the
# framebuffer base address (low 24 bits) was non-zero. With UEFI
# boot this is the GOP-provided FB; with BIOS rescue it's zero.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 15
expect_stage_reached 8 "graphics stage 2 done"
if [ "$E2E_BOOT_MODE" = "uefi" ]; then
    expect_stage_value 8 -gt 0 "framebuffer base non-zero (UEFI GOP)"
fi
echo "PASS: e2e/08 graphics ($E2E_BOOT_MODE)"
