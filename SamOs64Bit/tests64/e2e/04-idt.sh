#!/usr/bin/env bash
# e2e/04-idt.sh - idt_init returned without faulting.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 20
expect_stage_reached 3 "multiheap"
expect_stage_reached 4 "IDT live"
echo "PASS: e2e/04 idt"
