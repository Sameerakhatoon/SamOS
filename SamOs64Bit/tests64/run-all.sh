#!/usr/bin/env bash
# tests64/run-all.sh - walk every Lxx*.sh and report a
# pass/fail summary plus the list of failing test names.
#
# Usage:
#   bash tests64/run-all.sh
#
# Exit status is 0 even if some tests fail, so the caller can
# tee the output without losing it; check the count line or
# grep '^FAIL:' to decide whether to escalate.
#
# A full sweep is sequential. The first test pays the full
# build cost; subsequent tests are incremental (~1-2s each).
# A clean sweep takes about 5-7 minutes on a warm checkout.
set -u
cd "$(dirname "$0")"

pass=0
fail=0
failed=()

for f in L*.sh; do
    if bash "$f" >/dev/null 2>&1; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        failed+=("$f")
    fi
done

echo "$pass passed, $fail failed"
if [ "$fail" -gt 0 ]; then
    printf 'FAIL: %s\n' "${failed[@]}"
fi
