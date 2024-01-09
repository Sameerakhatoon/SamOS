#!/usr/bin/env bash
# e2e/27-path-parser.sh - pathparser_parse("@:/BLANK.ELF") returns
# a root with at least one path part.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 20
expect_stage_value 33 -eq 1 "pathparser_parse(@:/BLANK.ELF) returns root with first part"
echo "PASS: e2e/27 path parser"
