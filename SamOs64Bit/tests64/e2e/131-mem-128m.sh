#!/usr/bin/env bash
# e2e/131-mem-128m.sh - boots with 128 MiB, where no single
# usable region reaches the book's 100 MiB minimum, and the memory
# self-tests still pass. The frame allocator saw about 76 MiB; since
# The kernel reclaims the 44 MiB of loader and boot-services memory it sees
# about 120 MiB.
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_mem_lib.sh"
mem_boot_and_check 128M 40 110
echo "PASS: e2e/131 memory manager at 128 MiB"
