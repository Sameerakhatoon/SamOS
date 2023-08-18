#!/usr/bin/env bash
# e2e/131-mem-128m.sh - boots with 128 MiB, where no single
# usable region reaches the book's 100 MiB minimum, and the memory
# self-tests still pass. OVMF keeps a good part of it, so the frame
# allocator sees about 76 MiB.
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_mem_lib.sh"
mem_boot_and_check 128M 40 64
echo "PASS: e2e/131 memory manager at 128 MiB"
