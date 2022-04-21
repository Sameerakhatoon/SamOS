#!/usr/bin/env bash
# e2e/78-framebuffer-front.sh - graphics_redraw_all composites
# the synthetic back-buffer pixels into the actual framebuffer.
# Together with 38-framebuffer-painted (back-buffer probes), this
# proves the L86-L93 framebuffer + composite chain works.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 97 -eq 1 "front framebuffer non-zero after graphics_redraw_all"
echo "PASS: e2e/78 framebuffer front buffer"
