#!/usr/bin/env bash
# e2e/130-mem-512m.sh - memory-manager self-tests (PFA, heap
# unit checks, slabs, 1000-cycle leak test, stress) at 512 MiB.
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_mem_lib.sh"
mem_boot_and_check 512M 40 400
echo "PASS: e2e/130 memory manager at 512 MiB"
