#!/usr/bin/env bash
# tests64/e2e/run-all.sh - run every e2e test (NN-name.sh) and
# print one pass/fail count.
#
# Usage: run-all.sh [-j N] [pattern...]
#   -j N      run N tests at once (default: nproc/2, at least 1).
#             Each test boots its own QEMU with its own temp dir.
#   pattern   only run tests whose file name contains one of these
#             substrings, e.g. `run-all.sh 01- serial`.
#
# A test passes when it exits 0. Output of failing tests is shown
# indented under their FAIL line. Exit status is 0 only if every
# selected test passed.
set -u
cd "$(dirname "$0")"

jobs=$(( $(nproc) / 2 ))
[ "$jobs" -ge 1 ] || jobs=1
if [ "${1:-}" = "-j" ]; then
    jobs=$2
    shift 2
fi

tests=()
for f in [0-9]*-*.sh; do
    if [ $# -eq 0 ]; then
        tests+=("$f")
    else
        for pat in "$@"; do
            case "$f" in *"$pat"*) tests+=("$f"); break ;; esac
        done
    fi
done

if [ ${#tests[@]} -eq 0 ]; then
    echo "run-all: no tests selected" >&2
    exit 1
fi

outdir=$(mktemp -d "${TMPDIR:-/tmp}/samos-run-all.XXXXXX")
trap 'rm -rf "$outdir"' EXIT

run_one() {
    local f=$1 outdir=$2
    if bash "$f" > "$outdir/$f.out" 2>&1; then
        echo PASS > "$outdir/$f.status"
    else
        echo FAIL > "$outdir/$f.status"
    fi
    echo "$(cat "$outdir/$f.status") $f"
}
export -f run_one

start=$(date +%s)
printf '%s\n' "${tests[@]}" | xargs -P "$jobs" -I{} bash -c 'run_one "$1" "$2"' _ {} "$outdir"

pass=0
fail=0
failed=()
for f in "${tests[@]}"; do
    if [ "$(cat "$outdir/$f.status" 2>/dev/null)" = PASS ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        failed+=("$f")
    fi
done

if [ "$fail" -gt 0 ]; then
    echo
    for f in "${failed[@]}"; do
        echo "FAIL $f"
        sed 's/^/    /' "$outdir/$f.out"
    done
fi

echo
echo "==== $pass passed, $fail failed (of ${#tests[@]}, $(( $(date +%s) - start )) s, -j $jobs) ===="
[ "$fail" = 0 ]
