#!/usr/bin/env bash
# e2e/62-user-parse-cmd.sh - L116 samos_parse_command tokenises
# "hello world" into a non-NULL command_argument list.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 83 -eq 1 "samos_parse_command returned non-NULL list"
echo "PASS: e2e/62 user parse_command"
