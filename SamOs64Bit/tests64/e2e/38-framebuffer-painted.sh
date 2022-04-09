#!/usr/bin/env bash
# e2e/38-framebuffer-painted.sh - the graphics back buffer has
# pixel content after boot, with a font-stroke signature and a
# horizontal-edge signature.
#
# This is a "framebuffer-snapshot lite": instead of a QEMU vnc +
# screendump + golden-image diff (which would catch L93 redraw-
# region clip bugs, L99 transparency, font_draw kerning, etc.),
# we look at the kernel-allocated back buffer directly via the
# kernel_selftest probe. That gives us a binary "graphics drew
# *something*" check + two cheap pattern signatures, which is
# enough to catch a regression where the chain stops producing
# pixels at all (an empty framebuffer reads as all zeros).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 39 -eq 1 "graphics back buffer has any non-zero pixel"
expect_stage_value 51 -eq 1 "back buffer has a font-stroke signature"
expect_stage_value 52 -eq 1 "back buffer has a horizontal-edge signature"
echo "PASS: e2e/38 framebuffer painted"
