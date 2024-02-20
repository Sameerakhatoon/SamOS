#!/usr/bin/env bash
# e2e/102-window-event-push.sh - L161 window_event_push enqueues
# a focus event on a fresh window without crashing.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 134 -eq 1 "window_event_push round trip"
echo "PASS: e2e/102 window event push"
