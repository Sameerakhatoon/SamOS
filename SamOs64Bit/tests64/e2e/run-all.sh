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
# A test passes when it exits 0, is skipped when it exits 77
# (e2e_skip: a tool it needs is missing). Output of failing tests is shown
# indented under their FAIL line. Exit status is 0 only if every
# selected test passed.
#
# E2E_ARCH=aarch64 runs the suite on that arch (_lib.sh, the
# hook _arch_<arch>.sh). A test that needs hardware the arch's machine
# does not have says so in one line,
#     # e2e-requires: <feature>... (<reason>)
# and is skipped, with its reason, when the hook's E2E_ARCH_PROVIDES
# lacks one of the features. x86_64 provides every feature but
# virtio-mmio. A word "a|b" needs either one.
set -u
cd "$(dirname "$0")"

jobs=$(( $(nproc) / 2 ))
[ "$jobs" -ge 1 ] || jobs=1
if [ "${1:-}" = "-j" ]; then
    jobs=$2
    shift 2
fi

E2E_ARCH="${E2E_ARCH:-x86_64}"
export E2E_ARCH
provides=$(E2E_ROOT="$(cd ../.. && pwd)" bash -c '. "./_arch_$E2E_ARCH.sh" && echo "$E2E_ARCH_PROVIDES"') || {
    echo "run-all: no e2e hook for E2E_ARCH=$E2E_ARCH" >&2
    exit 1
}

# missing_feature <test>: the first feature the test needs that this
# arch does not provide, or nothing. A word "a|b" is met by any
# one of its alternatives (x86|riscv64: a test with lines for both).
missing_feature() {
    local need f alt
    need=$(sed -n 's/^# e2e-requires: *\([^(]*\).*/\1/p' "$1" | head -n 1)
    for f in $need; do
        for alt in ${f//|/ }; do
            case " $provides " in *" $alt "*) continue 2 ;; esac
        done
        echo "$f"
        return
    done
}

tests=()
skipped=()
for f in [0-9]*-*.sh; do
    if [ $# -ne 0 ]; then
        sel=0
        for pat in "$@"; do
            case "$f" in *"$pat"*) sel=1; break ;; esac
        done
        [ $sel -eq 1 ] || continue
    fi
    if [ -n "$(missing_feature "$f")" ]; then
        skipped+=("$f")
    else
        tests+=("$f")
    fi
done

if [ ${#tests[@]} -eq 0 ] && [ ${#skipped[@]} -eq 0 ]; then
    echo "run-all: no tests selected" >&2
    exit 1
fi

for f in "${skipped[@]}"; do
    echo "SKIP $f: needs $(missing_feature "$f") ($(sed -n 's/^# e2e-requires:[^(]*(\(.*\)).*/\1/p' "$f" | head -n 1))"
done

outdir=$(mktemp -d "${TMPDIR:-/tmp}/samos-run-all.XXXXXX")
trap 'rm -rf "$outdir"' EXIT

# Exit 77 (e2e_skip) is a skip at run time: a tool the test
# needs is missing; its "SKIP: <reason>" line is shown.
run_one() {
    local f=$1 outdir=$2 rc
    bash "$f" > "$outdir/$f.out" 2>&1
    rc=$?
    case $rc in
    0) echo PASS > "$outdir/$f.status" ;;
    77) echo SKIP > "$outdir/$f.status" ;;
    *) echo FAIL > "$outdir/$f.status" ;;
    esac
    if [ $rc -eq 77 ]; then
        echo "SKIP $f: $(sed -n 's/^SKIP: //p' "$outdir/$f.out" | tail -n 1)"
    else
        echo "$(cat "$outdir/$f.status") $f"
    fi
}
export -f run_one

start=$(date +%s)
if [ ${#tests[@]} -gt 0 ]; then
    printf '%s\n' "${tests[@]}" | xargs -P "$jobs" -I{} bash -c 'run_one "$1" "$2"' _ {} "$outdir"
fi

pass=0
fail=0
failed=()
total=$(( ${#tests[@]} + ${#skipped[@]} ))
for f in "${tests[@]}"; do
    status=$(cat "$outdir/$f.status" 2>/dev/null)
    if [ "$status" = PASS ]; then
        pass=$((pass + 1))
    elif [ "$status" = SKIP ]; then
        skipped+=("$f")
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

skip_note=""
[ ${#skipped[@]} -eq 0 ] || skip_note=", ${#skipped[@]} skipped on $E2E_ARCH"
echo
echo "==== $pass passed, $fail failed$skip_note (of $total, $(( $(date +%s) - start )) s, -j $jobs) ===="
[ "$fail" = 0 ]
