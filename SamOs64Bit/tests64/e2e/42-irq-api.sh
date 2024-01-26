#!/usr/bin/env bash
# e2e/42-irq-api.sh - L67 IRQ_enable / IRQ_disable round trip.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 56 -eq 1 "IRQ_disable + IRQ_enable round trip on IRQ_KEYBOARD"
echo "PASS: e2e/42 irq api"
