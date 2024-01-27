#!/usr/bin/env bash
# The non-x86 boot tests: every tests64/arch/<arch>-*.sh whose
# QEMU and cross compiler are installed. Each prints
# "<arch> arch test: N checks, M failures" last. A missing toolchain
# is a SKIP line, not a failure, so x86-only machines stay green.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
status=0
built=" "
for t in "$here"/*-*.sh; do
    arch="$(basename "$t")"
    arch="${arch%%-*}"
    if ! command -v "qemu-system-$arch" > /dev/null || ! command -v "$arch-linux-gnu-gcc" > /dev/null; then
        echo "$arch arch test: SKIP (no qemu-system-$arch or $arch-linux-gnu-gcc)"
        continue
    fi
    # Each arch is built once here (make ARCH=<arch> through
    # arch-build.sh): the riscv64 tests boot bin/riscv64/kernel.img
    # without building it, which failed in a fresh worktree.
    if [ "${built#* $arch }" = "$built" ]; then
        built="$built$arch "
        if ! bash "$here/../../arch-build.sh" "$arch" > /tmp/samos-arch-build-$arch.$$.log 2>&1; then
            echo "FAIL: $arch build (log /tmp/samos-arch-build-$arch.$$.log)"
            status=1
            continue
        fi
    fi
    bash "$t" || status=1
done
exit $status
