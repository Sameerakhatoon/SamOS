#!/bin/bash
# tests/48-malloc-free.sh
#
# Ch 130 / 131 - behavioural test for the user-mode malloc + free
# syscalls (cmd 4 SYSTEM_COMMAND4_MALLOC, cmd 5 SYSTEM_COMMAND5_FREE).
#
# kernel_main loads a blank.elf instance with argv[0]="MF". Its main
# does: samos_malloc(100), samos_malloc(8000), samos_free(first),
# samos_malloc(100) again, then prints "MF-OK". If any malloc returns
# NULL or a syscall faults, we get "MF-NULL" instead (or no marker).
#
# Pass criteria:
#   1) "MF-OK" appears on the serial mirror.
#   2) the kernel keeps producing userland output after.

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log" 'MF-OK' 'Abc!'

grep -q 'MF-OK' "$log" || {
    echo "FAIL: 'MF-OK' not in serial mirror"
    if grep -q 'MF-NULL' "$log"; then
        echo "      ('MF-NULL' was printed - one of the samos_malloc calls returned NULL)"
    fi
    echo "      head 30 lines:"; head -30 "$log"
    exit 1
}

post=$(awk '/MF-OK/{found=1; next} found' "$log")
if ! echo "$post" | grep -qE 'Abc!|Testing!'; then
    echo "FAIL: no userland prints after MF-OK - cmd 4/5 path may have corrupted state"
    exit 1
fi

exit 0
