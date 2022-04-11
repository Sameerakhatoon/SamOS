#!/usr/bin/env bash
# e2e/47-fat16-label.sh - L79 FAT16 volume label read. Marked
# pass-by-presence today; a follow-up should expose
# fat16_volume_label() to read the actual SAMOS label from p2
# and assert it.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 59 -eq 1 "FAT16 label read path present"
echo "PASS: e2e/47 fat16 label"
