#!/usr/bin/env bash
# e2e/84-mouse-driver.sh - L132-L148 mouse abstraction: register
# a synthetic mouse driver, attach click + move handlers, fire
# events, and verify the handlers were invoked.
#
# Unblocks the L27-L44 Mouse-and-Graphics curriculum band that
# the matrix marked 'skipped (no driver loaded in headless)'.
# kernel_selftest now registers a no-op static mouse so the
# event-dispatch chain has a target to look at.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 104 -eq 1 "mouse_register(synthetic) returns 0"
expect_stage_value 105 -eq 1 "mouse_register_click_handler attached"
expect_stage_value 106 -eq 1 "mouse_register_move_handler attached"
expect_stage_value 107 -eq 1 "mouse_click dispatches to registered handler"
expect_stage_value 108 -eq 1 "mouse_moved dispatches to registered handler"
echo "PASS: e2e/84 mouse driver + click + move"
