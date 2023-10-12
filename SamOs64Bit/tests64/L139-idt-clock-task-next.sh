#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# idt_clock body must have both the test print and task_next.
awk '/^void idt_clock\(\)/,/^}$/{print}' "$ROOT/src/idt/idt.c" > /tmp/idt_clock_body.$$
grep -q 'outb(0x20, 0x20)' /tmp/idt_clock_body.$$ || fail "idt_clock: EOI write missing"
grep -q 'print("test' /tmp/idt_clock_body.$$ || fail "idt_clock: test print missing"
grep -q 'task_next()' /tmp/idt_clock_body.$$ || fail "idt_clock: task_next call missing"
rm -f /tmp/idt_clock_body.$$

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L139 idt_clock task_next"
