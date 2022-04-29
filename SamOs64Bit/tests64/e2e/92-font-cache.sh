#!/usr/bin/env bash
# e2e/92-font-cache.sh - second font_load returns the cached
# instance.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 123 -eq 1 "font_load is idempotent (cache hit)"
echo "PASS: e2e/92 font cache hit"
