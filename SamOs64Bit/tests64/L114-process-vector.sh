#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# Static array retired.
if grep -E 'static struct process\* processes\[' "$ROOT/src/task/process.c" > /dev/null; then
    fail "process.c: static array still present"
fi
if grep -E '^[^/]*processes\[' "$ROOT/src/task/process.c" | grep -v '^[[:space:]]*//' > /dev/null; then
    fail "process.c: array indexing on processes[] still in code"
fi

# Vector lives.
grep -q 'struct vector\* process_vector' "$ROOT/src/task/process.c" \
    || fail "process.c: process_vector missing"
grep -q 'process_system_init' "$ROOT/src/task/process.c" \
    || fail "process.c: process_system_init missing"
grep -q 'process_system_init' "$ROOT/src/task/process.h" \
    || fail "process.h: prototype missing"

# Init from kernel_main.
grep -q 'process_system_init()' "$ROOT/src/kernel.c" \
    || fail "kernel.c: process_system_init call missing"

# Order: process_system_init before isr80h_register_commands.
awk '/process_system_init\(\)/{p=NR} /isr80h_register_commands\(\)/{if(!p || NR<p) exit 1; exit 0}' \
    "$ROOT/src/kernel.c" \
    || fail "kernel.c: process_system_init must precede isr80h_register_commands"

# process_get_free_slot vector-walk; vector_overwrite present.
grep -q 'vector_push(process_vector' "$ROOT/src/task/process.c" \
    || fail "process.c: vector_push for new slot missing"
grep -q 'vector_overwrite(process_vector' "$ROOT/src/task/process.c" \
    || fail "process.c: vector_overwrite for slot install missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L114 process vector"
