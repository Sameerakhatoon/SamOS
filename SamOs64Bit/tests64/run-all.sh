#!/usr/bin/env bash
# tests64/run-all.sh - the single entry point for SamOS tests.
#
# Runs the e2e suite (tests64/e2e/[0-9]*-*.sh; the aarch64 device work: on x86_64, then on
# each other arch with an e2e hook), then the host unit
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
#                                      # about 15 min; in parallel the
#                                      # ports built here on virtio-mmio:
#                                      # aarch64-virtio, riscv64-virtio,
#                                      # riscv64-sv39-virtio)
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

# The same e2e suite on every other arch that has a hook
# (e2e/_arch_<arch>.sh) and whose QEMU and cross compiler are
# installed: the hook builds its image, then e2e/run-all.sh runs with
# E2E_ARCH set and the same arguments. Tests that need hardware the
# arch's machine lacks are skipped by their e2e-requires line.
arch_e2e_lines=()
arch_e2e_status=0
built_arches=()
for hook in "$here"/e2e/_arch_*.sh; do
    a=$(basename "$hook" .sh)
    a=${a#_arch_}
    [ "$a" = x86_64 ] && continue
    echo
    if ! command -v "qemu-system-$a" > /dev/null || ! command -v "$a-linux-gnu-gcc" > /dev/null; then
        arch_e2e_lines+=("e2e $a: SKIP (no qemu-system-$a or $a-linux-gnu-gcc)")
        continue
    fi
    if ! (E2E_ROOT="$here/.." && . "$hook" && e2e_arch_build) > "$log/build-$a" 2>&1; then
        tail -n 20 "$log/build-$a"
        arch_e2e_lines+=("e2e $a: FAIL (image build failed)")
        arch_e2e_status=1
        continue
    fi
    built_arches+=("$a")
    E2E_ARCH=$a bash "$here/e2e/run-all.sh" "$@" 2>&1 | tee "$log/e2e-$a"
    [ "${PIPESTATUS[0]}" -eq 0 ] || arch_e2e_status=1
    line=$(grep -E '^==== [0-9]+ passed' "$log/e2e-$a" | tail -n 1 | sed 's/^==== //; s/ ====$//')
    arch_e2e_lines+=("e2e $a: ${line:-no summary}")
done

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
# The timeout notes too, and a failing run's whole output
# (the serial dumps) is kept: an el1 boot that printed nothing in one
# full run left no trace of why.
grep -aE '^(PASS|FAIL|.* arch test:|  \(.*timed out)' "$log/arch"
arch_checks=$(grep -aE ' arch test: ' "$log/arch" | sed 's/ arch test:/:/' | paste -sd ';' - | sed 's/;/; /g')
if [ $arch -eq 0 ]; then
    arch_line="PASS ($arch_checks)"
else
    arch_keep=$(mktemp "${TMPDIR:-/tmp}/samos-arch-fail.XXXXXX.log")
    cp "$log/arch" "$arch_keep"
    arch_line="FAIL (exit $arch; ${arch_checks:-no summary}; serial logs: $arch_keep)"
fi

# The storage test boots its own four scratch disks twice
# per machine, so it runs after the e2e suite, not inside it.
storage=0
if [ $storage_wanted -eq 1 ]; then
    echo
    # Parity: each port whose image was built above runs the same
    # suite at the same time as x86, one run.sh (and QEMU) per group.
    groups=("pc q35 q35-virtio")
    for a in "${built_arches[@]}"; do
        case $a in
            aarch64) groups+=(aarch64-virtio) ;;
            riscv64) groups+=(riscv64-virtio riscv64-sv39-virtio) ;;
        esac
    done
    pids=()
    for i in "${!groups[@]}"; do
        # shellcheck disable=SC2086
        bash "$here/storage/run.sh" ${groups[$i]} > "$log/storage-$i" 2>&1 &
        pids+=($!)
    done
    passed=() failed=()
    for i in "${!groups[@]}"; do
        wait "${pids[$i]}" || storage=1
        cat "$log/storage-$i"
        l=$(grep -E '^storage: ' "$log/storage-$i" | tail -n 1 | sed 's/^storage: //')
        case $l in
            PASS*) passed+=("${groups[$i]}") ;;
            *) failed+=("${groups[$i]}: ${l:-no summary}") ;;
        esac
    done
    if [ $storage -eq 0 ]; then
        storage_line="PASS (${passed[*]})"
    else
        storage_line="FAIL ($(printf '%s; ' "${failed[@]}")passed: ${passed[*]:-none})"
    fi
fi

echo
echo "==== summary ===="
echo "e2e:     ${e2e_line:-no summary (exit $e2e)}"
for l in "${arch_e2e_lines[@]}"; do
    echo "$l"
done
echo "host:    $host_line"
echo "arch:    $arch_line"
if [ $storage_wanted -eq 1 ]; then
    echo "storage: ${storage_line:-no summary (exit $storage)}"
fi
[ $e2e -eq 0 ] && [ $arch_e2e_status -eq 0 ] && [ $host -eq 0 ] && [ $arch -eq 0 ] && [ $storage -eq 0 ]
