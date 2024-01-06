#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q 'process_is_stack_memory' "$ROOT/src/task/process.c" \
    || fail "process.c: is_stack_memory missing"
grep -q 'process_get_allocation_by_addr' "$ROOT/src/task/process.c" \
    || fail "process.c: get_allocation_by_addr missing"

# validate body now references the request walk (no longer a stub).
awk '/^int process_validate_memory_or_terminate\(/,/^}$/{print}' \
    "$ROOT/src/task/process.c" | grep -q 'process_get_allocation_by_addr' \
    || fail "process.c: validate must call get_allocation_by_addr"

# Header decls.
grep -q 'process_get_allocation_by_addr' "$ROOT/src/task/process.h" \
    || fail "process.h: prototype missing"
grep -q 'process_is_stack_memory' "$ROOT/src/task/process.h" \
    || fail "process.h: is_stack_memory missing"
grep -q 'process_validate_memory_or_terminate' "$ROOT/src/task/process.h" \
    || fail "process.h: validate missing"

# Stack constants used (verbatim names ok per project; could be either).
grep -q 'SAMOS_PROGRAM_VIRTUAL_STACK_ADDRESS_START' "$ROOT/src/task/process.c" \
    || fail "process.c: stack START constant missing"

# Allocations vector_free at process_terminate.
grep -q 'vector_free(process->allocations)' "$ROOT/src/task/process.c" \
    || fail "process.c: allocations vector_free missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L109 process validate"
