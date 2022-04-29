#!/usr/bin/env bash
# e2e/90-keyboard-buffer.sh - L83 virtual keyboard buffer push/pop
# round trip.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 120 -eq 1 "keyboard_push('Q') + keyboard_pop() == 'Q'"
echo "PASS: e2e/90 keyboard buffer"
