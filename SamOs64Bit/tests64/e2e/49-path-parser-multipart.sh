#!/usr/bin/env bash
# e2e/49-path-parser-multipart.sh - L43 path parser handles a
# multi-component path (two or more path_parts).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 63 -eq 1 "pathparser_parse(@:/dir/file) returns 2+ parts"
echo "PASS: e2e/49 path parser multipart"
