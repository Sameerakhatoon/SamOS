#!/usr/bin/env bash
# e2e/119-streamer-seek-512.sh - diskstreamer_seek to a 512-byte
# offset returns >= 0. (SamOs's seek does not validate negative
# offsets; that path is documented in the matrix.)
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 163 -eq 1 "diskstreamer_seek(s, 512) >= 0"
echo "PASS: e2e/119 streamer seek 512"
