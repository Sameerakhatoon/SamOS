#!/usr/bin/env bash
# e2e/41-user-get-event.sh - L163 drain a window event ring.
# Asserts samos_get_window_event returned to userland without
# faulting on an empty ring.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 55 -eq 1 "samos_get_window_event round trip"
echo "PASS: e2e/41 user get_window_event"
