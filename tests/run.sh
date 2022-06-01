#!/bin/bash
# Top-level test runner. Walks tests/*.sh in numeric order, runs each, prints
# pass/fail. Exit code = number of failed tests.
#
# Per-test conventions:
#   - File name: tests/NN-topic.sh (e.g. tests/01-bootloader-prints-hello.sh)
#   - Each script exits 0 on pass, non-zero on fail.
#   - Each script self-contained: builds what it needs, runs QEMU, greps output.
#   - Tests don't depend on each other; ordering is just for readability.

set -u

cd "$(dirname "$0")/.."

passed=0
failed=0
failed_list=()

for t in tests/[0-9][0-9]-*.sh; do
    [ -f "$t" ] || continue
    name="$(basename "$t" .sh)"
    printf '  %s ... ' "$name"
    if bash "$t" > "/tmp/$name.log" 2>&1; then
        echo "ok"
        passed=$((passed + 1))
    else
        echo "FAIL"
        failed=$((failed + 1))
        failed_list+=("$name")
        echo "      --- log tail ---"
        tail -20 "/tmp/$name.log" | sed 's/^/      /'
    fi
done

echo
echo "passed: $passed   failed: $failed"
if [ $failed -gt 0 ]; then
    echo "failed tests:"
    for n in "${failed_list[@]}"; do echo "  $n"; done
fi
exit $failed
