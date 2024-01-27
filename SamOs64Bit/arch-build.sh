#!/bin/bash
# arch-build.sh - build a non-x86 SamOS kernel.
#
#   bash arch-build.sh aarch64      # -> bin/aarch64/kernel.img
#   bash arch-build.sh riscv64      # -> bin/riscv64/kernel.img
#
# A thin wrapper around `make ARCH=<arch>` (the Makefile holds
# the flags, the object lists and the cross compilers), kept because
# tests64/arch/ calls it. Like build.sh it starts clean every time:
# the Makefile has no header dependencies. Objects and kernel.elf are
# in build/<arch>/, which the x86 `make clean` does not touch.
# CROSS=aarch64-elf- (say) switches to a bare-metal toolchain.
set -euo pipefail
cd "$(dirname "$0")"

arch="${1:-}"
case "$arch" in
aarch64|riscv64) ;;
*)
    echo "usage: $0 aarch64|riscv64" >&2
    exit 2
    ;;
esac

make_args=(ARCH="$arch")
[ -n "${CROSS:-}" ] && make_args+=(CROSS="$CROSS")
make "${make_args[@]}" clean > /dev/null
make -j"$(nproc)" "${make_args[@]}"
echo "arch-build: bin/$arch/kernel.img ($(stat -c%s "bin/$arch/kernel.img") bytes)"
