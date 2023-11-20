#!/usr/bin/env bash
# The non-x86 boot tests: every tests64/arch/<arch>-*.sh whose
# QEMU and cross compiler are installed. Each prints
# "<arch> arch test: N checks, M failures" last. A missing toolchain
# is a SKIP line, not a failure, so x86-only machines stay green.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
status=0
for t in "$here"/*-*.sh; do
    arch="$(basename "$t")"
    arch="${arch%%-*}"
    if ! command -v "qemu-system-$arch" > /dev/null || ! command -v "$arch-linux-gnu-gcc" > /dev/null; then
        echo "$arch arch test: SKIP (no qemu-system-$arch or $arch-linux-gnu-gcc)"
        continue
    fi
    bash "$t" || status=1
done
exit $status
