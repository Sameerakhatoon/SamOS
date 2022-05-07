#!/usr/bin/env bash
# e2e/113-kernel-desc-and-paths.sh - kernel paging level + path
# parser with explicit drive specifier + defensive NULL handling.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 154 -eq 1 "kernel_desc level set"
expect_stage_value 155 -eq 1 "pathparser_parse(\"0:/foo\") returns non-NULL"
expect_stage_value 156 -eq 1 "pathparser_free(NULL) no-op"
echo "PASS: e2e/113 kernel_desc + path drive + free(NULL)"
