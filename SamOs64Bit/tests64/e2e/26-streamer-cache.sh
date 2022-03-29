#!/usr/bin/env bash
# e2e/26-streamer-cache.sh - disk stream cache allocator returns non-NULL.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 20
expect_stage_value 32 -eq 1 "diskstreamer_cache_new returns non-NULL"
echo "PASS: e2e/26 streamer cache"
