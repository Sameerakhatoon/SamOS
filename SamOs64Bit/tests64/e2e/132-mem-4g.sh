#!/usr/bin/env bash
# e2e/132-mem-4g.sh - boots with 4 GiB, so part of RAM sits
# above the 4 GiB line, and the memory self-tests still pass.
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_mem_lib.sh"
mem_boot_and_check 4G 90 3500
echo "PASS: e2e/132 memory manager at 4 GiB"
