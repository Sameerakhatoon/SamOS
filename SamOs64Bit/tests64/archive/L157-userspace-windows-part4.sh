#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# idt_clock now guards on task_current().
awk '/^void idt_clock/,/^}$/' "$ROOT/src/idt/idt.c" \
    | grep -q 'if(!task_current())' \
    || fail "idt.c: idt_clock missing task_current guard"

# interrupt_handler re-enables save_state + task_page, both gated.
awk '/^void interrupt_handler/,/^}$/' "$ROOT/src/idt/idt.c" \
    > /tmp/L157_handler.$$
grep -q 'task_current_save_state(frame)' /tmp/L157_handler.$$ \
    || fail "idt.c: task_current_save_state call missing"
grep -q 'task_page()' /tmp/L157_handler.$$ \
    || fail "idt.c: task_page call missing"
count_if=$(grep -c 'if(task_current())' /tmp/L157_handler.$$)
[ "$count_if" -ge 2 ] || fail "idt.c: expected two task_current guards in handler"
rm -f /tmp/L157_handler.$$

# SamOs already shipped with paging_null_entry(pdpt_entry) (not _entries).
grep -q 'paging_null_entry(pdpt_entry)' "$ROOT/src/memory/paging/paging.c" \
    || fail "paging.c: pdpt_entry check missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L157 userspace windows part 4"
