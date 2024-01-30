#!/usr/bin/env bash
# e2e/60-user-putchar.sh - L107 samos_putchar trampoline.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 82 -eq 1 "samos_putchar('!') round trip"
echo "PASS: e2e/60 user putchar"
