#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# Header + impl exist.
[ -f "$ROOT/src/task/userlandptr.h" ] || fail "userlandptr.h missing"
[ -f "$ROOT/src/task/userlandptr.c" ] || fail "userlandptr.c missing"

# Four API entry points are declared.
for sym in process_userland_pointer_create \
           process_userland_pointer_release \
           process_userland_pointer_registered \
           process_userland_pointer_kernel_ptr; do
    grep -q "$sym" "$ROOT/src/task/userlandptr.h" || fail "header: $sym missing"
    grep -q "^[a-zA-Z].* $sym(" "$ROOT/src/task/userlandptr.c" \
        || grep -q "^$sym(" "$ROOT/src/task/userlandptr.c" \
        || fail "impl: $sym body missing"
done

# struct userland_ptr defined.
grep -q 'struct userland_ptr {' "$ROOT/src/task/userlandptr.h" \
    || fail "struct userland_ptr missing"

# Process gains the registry field, inits it, and frees it.
grep -q 'kernel_userland_ptrs_vector' "$ROOT/src/task/process.h" \
    || fail "process.h: field missing"
grep -q 'kernel_userland_ptrs_vector =' "$ROOT/src/task/process.c" \
    || fail "process.c: init missing"
grep -q 'vector_free(process->kernel_userland_ptrs_vector)' \
    "$ROOT/src/task/process.c" \
    || fail "process.c: teardown missing"

# Makefile links userlandptr.o and ships a recipe.
grep -q 'build/task/userlandptr.o' "$ROOT/Makefile" \
    || fail "Makefile: userlandptr.o missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L153 userland pointers"
