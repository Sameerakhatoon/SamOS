#!/usr/bin/env bash
# Shared checks for the memory-manager e2e tests. Sourced
# after _lib.sh. Not a test itself (no NN- prefix).
#
# mem_boot_and_check <ram> <seconds> <min_mib>
#   Boots with -m <ram>, then asserts that kernel_main got past the
#   multiheap, that every BM_FEATURE_MEM_* self-test passed (a
#   failing one stores its memtest.c line number instead of 1), and
#   that the frame allocator manages at least <min_mib> MiB, including
#   the reclaimed loader memory.

mem_boot_and_check() {
    local ram=$1 seconds=$2 min_mib=$3
    export E2E_MEM=$ram
    boot_and_dump "$seconds"
    expect_stage_reached 3 "multiheap ready ($ram)"
    expect_stage_value 176 -eq 1 "pfa reserves kernel, boot stack, map, frame table ($ram)"
    expect_stage_value 177 -eq 1 "heap.c unit checks ($ram)"
    expect_stage_value 178 -eq 1 "kmalloc slabs + bad kfree rejected ($ram)"
    expect_stage_value 179 -eq 1 "1000 alloc/free cycles leave the heap steady ($ram)"
    expect_stage_value 180 -eq 1 "heap stress ($ram)"
    expect_stage_value 182 -eq 1 "process create/exit cycles: frames steady, constant leak ($ram)"
    # Fopen leaks the parsed path (3 objects) and
    # process_terminate leaks struct process (1). Tighten to -eq 0
    # once the storage and core tracks fix them.
    expect_stage_value 183 -le 4 "slab objects leaked per process cycle ($ram)"
    expect_stage_value 181 -ge "$min_mib" "frame allocator manages >= $min_mib MiB ($ram)"
    # Loader and boot-services memory goes to the PFA.
    expect_serial '^pfa: reclaimed [1-9][0-9]* KiB of loader memory' "loader memory reclaimed ($ram)"
}
