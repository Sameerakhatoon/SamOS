#!/usr/bin/env bash
# tests64/run-all.sh - the single entry point for SamOS tests.
#
# Runs the e2e suite (tests64/e2e/[0-9]*-*.sh), then the host unit
# tests (tests64/host/run.sh, host gcc, a few seconds), then the
# non-x86 boot tests (tests64/arch/run.sh, about 10 s; skipped
# when the cross tools are missing), and ends with one summary line
# per part. Arguments are passed through to
# e2e/run-all.sh:
#
#   bash tests64/run-all.sh            # everything, parallel
#   bash tests64/run-all.sh -j 1       # one at a time
#   bash tests64/run-all.sh 01- 31-    # a subset by name (host tests still run)
#   bash tests64/run-all.sh --storage -j 3
#                                      # also tests64/storage/run.sh
#                                      # (pc, q35 and q35 on virtio-blk,
#                                      # about 15 min)
#
# Build first with ./build.sh at the repo root.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
img="$here/../../bin/os.img"
if [ ! -f "$img" ]; then
    echo "tests64/run-all.sh: $img is missing; run ./build.sh first" >&2
    exit 2
fi
storage_wanted=0
if [ "${1:-}" = --storage ]; then
    storage_wanted=1
    shift
fi
log="$(mktemp -d)"
trap 'rm -rf "$log"' EXIT

bash "$here/e2e/run-all.sh" "$@" 2>&1 | tee "$log/e2e"
e2e=${PIPESTATUS[0]}
e2e_line=$(grep -E '^==== [0-9]+ passed' "$log/e2e" | tail -n 1 | sed 's/^==== //; s/ ====$//')

# The host tests are part of the one count.
echo
bash "$here/host/run.sh" > "$log/host" 2>&1
host=$?
cat "$log/host"
host_checks=$(grep -E 'host test: [0-9]+ checks' "$log/host" | sed 's/ host test:/:/' | paste -sd ';' - | sed 's/;/; /g')
if [ $host -eq 0 ]; then
    host_line="PASS ($host_checks)"
else
    host_line="FAIL (exit $host; ${host_checks:-no summary})"
fi

# aarch64 (later riscv64) direct -kernel boots. Its own
# build, its own QEMU; nothing here touches bin/os.img.
echo
bash "$here/arch/run.sh" > "$log/arch" 2>&1
arch=$?
grep -aE '^(PASS|FAIL|.* arch test:)' "$log/arch"
arch_checks=$(grep -aE ' arch test: ' "$log/arch" | sed 's/ arch test:/:/' | paste -sd ';' - | sed 's/;/; /g')
if [ $arch -eq 0 ]; then
    arch_line="PASS ($arch_checks)"
else
    arch_line="FAIL (exit $arch; ${arch_checks:-no summary}; serial logs: bash tests64/arch/run.sh)"
fi

# The storage test boots its own four scratch disks twice
# per machine, so it runs after the e2e suite, not inside it.
storage=0
if [ $storage_wanted -eq 1 ]; then
    echo
    bash "$here/storage/run.sh" pc q35 q35-virtio 2>&1 | tee "$log/storage"
    storage=${PIPESTATUS[0]}
    storage_line=$(grep -E '^storage: ' "$log/storage" | tail -n 1 | sed 's/^storage: //')
fi

echo
echo "==== summary ===="
echo "e2e:     ${e2e_line:-no summary (exit $e2e)}"
echo "host:    $host_line"
echo "arch:    $arch_line"
if [ $storage_wanted -eq 1 ]; then
    echo "storage: ${storage_line:-no summary (exit $storage)}"
fi
[ $e2e -eq 0 ] && [ $host -eq 0 ] && [ $arch -eq 0 ] && [ $storage -eq 0 ]
