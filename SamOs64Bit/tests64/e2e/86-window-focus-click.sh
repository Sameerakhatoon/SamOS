#!/usr/bin/env bash
# e2e/86-window-focus-click.sh - L143-L146 kernel window focus +
# click dispatch + event handler register/unregister.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 113 -eq 1 "window_focus does not crash"
expect_stage_value 114 -eq 1 "window_click dispatches"
expect_stage_value 115 -eq 1 "window_event_handler_register attached"
expect_stage_value 116 -eq 1 "window_event_handler_unregister round trip"
echo "PASS: e2e/86 window focus + click + handler ring"
