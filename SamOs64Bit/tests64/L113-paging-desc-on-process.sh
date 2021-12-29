#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# struct process gains paging_desc.
grep -q 'struct paging_desc\* paging_desc' "$ROOT/src/task/process.h" \
    || fail "process.h: paging_desc field missing"

# struct task drops it.
if grep -E 'struct paging_desc\*[[:space:]]+paging_desc' "$ROOT/src/task/task.h" > /dev/null; then
    fail "task.h: paging_desc field should be removed"
fi

# task.c forwards through process.
grep -q 'task->process->paging_desc' "$ROOT/src/task/task.c" \
    || fail "task.c: must forward through process->paging_desc"
if grep -E 'task->paging_desc' "$ROOT/src/task/task.c" > /dev/null; then
    fail "task.c: stale task->paging_desc"
fi

# process.c uses process->paging_desc.
grep -q 'process->paging_desc' "$ROOT/src/task/process.c" \
    || fail "process.c: must use process->paging_desc"
if grep -E 'process->task->paging_desc' "$ROOT/src/task/process.c" > /dev/null; then
    fail "process.c: stale process->task->paging_desc"
fi

# process_load_for_slot creates paging_desc BEFORE task_new.
awk '/process_load_for_slot/,/^}$/{print}' "$ROOT/src/task/process.c" \
    | awk '/paging_desc_new/{d=NR} /task_new\(/{if(!d || NR<d) exit 1; exit 0}' \
    || fail "process_load_for_slot: paging_desc_new must precede task_new"

# Teardown frees paging_desc.
grep -q 'paging_desc_free(process->paging_desc)' "$ROOT/src/task/process.c" \
    || fail "process.c: paging_desc_free missing from teardown"
if grep -q 'paging_desc_free(task->paging_desc)' "$ROOT/src/task/task.c"; then
    fail "task.c: stale paging_desc_free in task_free"
fi

# Forward decl in process.h.
grep -q 'struct paging_desc;' "$ROOT/src/task/process.h" \
    || fail "process.h: forward decl of struct paging_desc missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L113 paging_desc moved onto process"
