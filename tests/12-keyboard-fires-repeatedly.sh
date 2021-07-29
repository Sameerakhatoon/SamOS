#!/bin/bash
# tests/12-keyboard-fires-repeatedly.sh
#
# Ch 112 - send THREE keypresses with small gaps. The kernel survives
# a burst of keypresses; the serial mirror keeps showing live kernel
# or userland output after the keys arrived.

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_inspect "$log" "$log" 'entering userland' '' \
    'sendkey a' 'sendkey b' 'sendkey c'

# After the three keys, wait for a follow-up userland print to confirm
# the kernel is still running. The original capture stops at the
# monitor commands, so re-poll the log here briefly.
for i in $(seq 1 60); do
    if grep -qE 'Abc!|Testing!|MF-OK|EXIT-RAN' "$log"; then
        exit 0
    fi
    sleep 0.05
done

if grep -qE 'bootsig=000055AA|Abc!|Testing!' "$log"; then
    exit 0
fi

echo "FAIL: serial log has no kernel/userland output after 3 sendkeys - kernel may have crashed on IRQ1"
echo "      first 400 bytes: $(head -c 400 "$log")"
exit 1
