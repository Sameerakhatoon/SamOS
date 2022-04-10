#!/usr/bin/env bash
# e2e/40-user-sysout-redir.sh - L158 divert stdout to a window.
# Asserts samos_sysout_to_window completed without crashing the task.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 54 -eq 1 "samos_sysout_to_window did not fault"
echo "PASS: e2e/40 user sysout redir"
