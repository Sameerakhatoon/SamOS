#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# struct task gains the sleeping field.
grep -q 'sleep_until_microseconds' "$ROOT/src/task/task.h" \
    || fail "task.h: sleep_until_microseconds field missing"

for sym in task_sleep task_asleep task_get_next_non_sleeping_task; do
    grep -q "$sym" "$ROOT/src/task/task.h" || fail "task.h: $sym decl missing"
    grep -q "$sym" "$ROOT/src/task/task.c" || fail "task.c: $sym body missing"
done

# task_next now loops on non-sleeping tasks.
grep -q 'task_get_next_non_sleeping_task' "$ROOT/src/task/task.c" \
    || fail "task.c: task_next rewrite missing"
grep -q 'udelay(100)' "$ROOT/src/task/task.c" \
    || fail "task.c: udelay backoff missing"

# Upstream bug preserved verbatim (tsc_microseconds() * microseconds).
grep -q 'tsc_microseconds() \* microseconds' "$ROOT/src/task/task.c" \
    || fail "task.c: upstream multiplication bug not preserved"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L197 task sleep"
