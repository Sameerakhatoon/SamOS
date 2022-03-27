#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# struct process_allocation has new end field.
grep -q 'struct process_allocation' "$ROOT/src/task/process.h" || fail "struct missing"
grep -q 'void\*  end' "$ROOT/src/task/process.h" || fail "end field missing"

# process_allocation_request shape.
grep -q 'struct process_allocation_request' "$ROOT/src/task/process.h" \
    || fail "process_allocation_request missing"
grep -q 'PROCESS_ALLOCATION_REQUEST_IS_STACK_MEMORY' "$ROOT/src/task/process.h" \
    || fail "stack flag missing"
grep -q 'total_bytes_left' "$ROOT/src/task/process.h" \
    || fail "peek total_bytes_left missing"

# Allocations field is now a vector.
grep -q 'struct vector\*     allocations' "$ROOT/src/task/process.h" \
    || fail "allocations field not vector"

# Vector-backed init.
grep -q 'process->allocations  = vector_new' "$ROOT/src/task/process.c" \
    || fail "process_init must vector_new(allocations)"

# New helpers.
grep -q 'process_allocation_set_map' "$ROOT/src/task/process.c" \
    || fail "process_allocation_set_map missing"
grep -q 'process_get_allocation_by_start_addr' "$ROOT/src/task/process.c" \
    || fail "by_start_addr missing"

# No more array indexing on allocations in code (comments may
# reference the historic upstream form).
if grep -E '^[^/]*process->allocations\[' "$ROOT/src/task/process.c" \
    | grep -v '^[[:space:]]*//' > /dev/null; then
    fail "process.c: array indexing on allocations remains in code"
fi

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L108 process allocations vector"
