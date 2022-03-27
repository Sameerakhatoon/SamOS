#!/usr/bin/env bash
# L80 - krealloc real implementation, routed through
# multiheap_realloc -> heap_realloc.
#
# Source-only checks: BIOS boot is broken since L74 (UEFI
# pivot); we cannot run the kernel in QEMU here.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)

fail(){ echo "FAIL: $*" >&2; exit 1; }

# heap.h must declare the new helpers.
grep -q 'heap_is_block_range_free' "$ROOT/src/memory/heap/heap.h" \
    || fail "heap.h: heap_is_block_range_free prototype missing"
grep -q 'heap_realloc' "$ROOT/src/memory/heap/heap.h" \
    || fail "heap.h: heap_realloc prototype missing"

# heap.c must define them.
grep -q 'heap_is_block_range_free(' "$ROOT/src/memory/heap/heap.c" \
    || fail "heap.c: heap_is_block_range_free definition missing"
grep -q 'heap_realloc(' "$ROOT/src/memory/heap/heap.c" \
    || fail "heap.c: heap_realloc definition missing"

# Three cases (shrink, grow in place, grow with move).
grep -qi 'shrink'          "$ROOT/src/memory/heap/heap.c" \
    || fail "heap.c: shrink case marker missing"
grep -qi 'grow in place'   "$ROOT/src/memory/heap/heap.c" \
    || fail "heap.c: grow-in-place case marker missing"
grep -qi 'grow with move'  "$ROOT/src/memory/heap/heap.c" \
    || fail "heap.c: grow-with-move case marker missing"

# multiheap dispatch wired up.
grep -q 'multiheap_realloc' "$ROOT/src/memory/heap/multiheap.h" \
    || fail "multiheap.h: multiheap_realloc prototype missing"
grep -q 'multiheap_realloc(' "$ROOT/src/memory/heap/multiheap.c" \
    || fail "multiheap.c: multiheap_realloc definition missing"

# kheap krealloc forwards to multiheap_realloc (L78 stub gone).
grep -q 'return multiheap_realloc(kernel_multiheap' "$ROOT/src/memory/heap/kheap.c" \
    || fail "kheap.c: krealloc must forward to multiheap_realloc"
grep -q 'krealloc' "$ROOT/src/memory/heap/kheap.h" \
    || fail "kheap.h: krealloc prototype missing"

# Build still links.
( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) \
    || fail "build.sh failed"

echo "PASS: L80 krealloc"
