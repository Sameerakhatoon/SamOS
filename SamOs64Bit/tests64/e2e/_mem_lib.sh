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
    # samos.memtest=full runs the 1000-cycle process leak test.
    cp "$E2E_IMAGE" "$E2E_TMP/os.img"
    E2E_IMAGE="$E2E_TMP/os.img"
    printf 'samos.memtest=full\n' > "$E2E_TMP/CMDLINE.TXT"
    MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" \
        ::/CMDLINE.TXT || e2e_fail "mcopy CMDLINE.TXT"
    boot_and_dump "$seconds"
    expect_stage_reached 3 "multiheap ready ($ram)"
    expect_stage_value 176 -eq 1 "pfa reserves kernel, boot stack, map, frame table ($ram)"
    expect_stage_value 177 -eq 1 "heap.c unit checks ($ram)"
    expect_stage_value 178 -eq 1 "kmalloc slabs + bad kfree rejected ($ram)"
    expect_stage_value 179 -eq 1 "1000 alloc/free cycles leave the heap steady ($ram)"
    expect_stage_value 180 -eq 1 "heap stress ($ram)"
    expect_stage_value 184 -eq 1 "virtual-arena alloc/free returns frames and unmaps ($ram)"
    expect_serial '^memtest arena: ok$' "memtest arena line ($ram)"
    expect_stage_value 182 -eq 1 "process create/exit cycles: frames steady, constant leak ($ram)"
    # Both leaks are fixed (fopen, struct process).
    expect_serial '^memtest process: 1000 create/exit cycles in [0-9]+ ms, frames 0, slab objects 0$' \
        "1000 process create/exit cycles with a steady heap ($ram)"
    expect_stage_value 183 -eq 0 "slab objects leaked over 1000 process cycles ($ram)"
    expect_stage_value 181 -ge "$min_mib" "frame allocator manages >= $min_mib MiB ($ram)"
    # Loader and boot-services memory goes to the PFA. The aarch64 device work:
    # only a UEFI boot leaves any (aarch64 boots with -kernel).
    if e2e_arch_has uefi; then
        expect_serial '^pfa: reclaimed [1-9][0-9]* KiB of loader memory' "loader memory reclaimed ($ram)"
    fi
}
