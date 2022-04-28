#!/usr/bin/env bash
# e2e/87-streamer-cache-hit.sh - L99-L103 disk streamer cache
# allocator stability across a second allocation.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 117 -eq 1 "second diskstreamer_cache_new returns non-NULL"
echo "PASS: e2e/87 streamer cache stability"
