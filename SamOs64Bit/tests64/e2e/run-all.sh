#!/usr/bin/env bash
# tests64/e2e/run-all.sh - walk every e2e test (NN-name.sh) and
# report pass/fail. Each one boots QEMU end-to-end so the full
# sweep takes ~90 seconds.
set -u
cd "$(dirname "$0")"

pass=0
fail=0
failed=()

for f in [0-9]*-*.sh; do
    name=$(basename "$f")
    if out=$(bash "$f" 2>&1); then
        pass=$((pass + 1))
        echo "PASS $name"
    else
        fail=$((fail + 1))
        failed+=("$name")
        echo "FAIL $name"
        echo "$out" | sed 's/^/    /'
    fi
done

echo
echo "==== $pass passed, $fail failed ===="
[ "$fail" = 0 ]
