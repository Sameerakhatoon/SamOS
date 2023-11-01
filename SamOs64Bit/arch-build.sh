#!/bin/bash
# arch-build.sh - build a non-x86 SamOS kernel (the first riscv64 spike).
#
#   bash arch-build.sh riscv64      # -> bin/riscv64/kernel.img
#
# Separate from build.sh and the Makefile on purpose: the x86 build
# and its outputs are untouched until the hardware layer adds `make ARCH=...`.
# Objects and kernel.elf go to build/<arch>/, which `make clean` does
# not touch. Same interface as the aarch64 case on s-a1-arm64.
#
# Toolchain: the Ubuntu cross compiler riscv64-linux-gnu-gcc, used
# freestanding (no libc, no crt, own linker script). The plan named
# riscv64-elf-gcc; the Linux triplet produces the same code with
# these flags, so it stands in until bootstrap.sh builds the elf one.
set -euo pipefail
cd "$(dirname "$0")"

arch="${1:-}"
case "$arch" in
riscv64)
    CROSS="${CROSS:-riscv64-linux-gnu-}"
    # rv64imac: no F/D, so the kernel has no FP state to save yet.
    # medany: the kernel sits at 0x80200000, past medlow's +-2 GiB.
    # -fno-tree-loop-distribute-patterns: the BSS-style loops must
    #   not turn into memset calls, there is no libc.
    ARCH_CFLAGS="-march=rv64imac_zicsr -mabi=lp64 -mcmodel=medany -fno-tree-loop-distribute-patterns"
    # One RWX segment until the riscv64 MMU work brings W^X.
    ARCH_LDFLAGS="-Wl,--no-warn-rwx-segments"
    SRCS="src/arch/riscv64/boot.S src/arch/riscv64/main.c src/arch/riscv64/sbi.c
          src/arch/riscv64/console.c src/arch/riscv64/trap.c src/arch/riscv64/uart16550.c
          src/firmware/fdt.c src/string/string.c"
    ;;
*)
    echo "usage: $0 riscv64" >&2
    exit 2
    ;;
esac

CC="${CROSS}gcc"
OBJCOPY="${CROSS}objcopy"
CFLAGS="-ffreestanding -fno-builtin -nostdlib -fno-pic -fno-pie -fno-stack-protector
        -fno-asynchronous-unwind-tables -O2 -g -std=gnu99 -Wall -Wextra -Werror
        -Wno-unused-parameter -Isrc $ARCH_CFLAGS"

out="build/$arch"
mkdir -p "$out" "bin/$arch"
objs=()
for src in $SRCS; do
    obj="$out/$(basename "$src").o"
    $CC $CFLAGS -c "$src" -o "$obj"
    objs+=("$obj")
done
$CC $CFLAGS -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack ${ARCH_LDFLAGS:-} \
    -T "src/arch/$arch/linker.ld" -o "$out/kernel.elf" "${objs[@]}" -lgcc
$OBJCOPY -O binary "$out/kernel.elf" "bin/$arch/kernel.img"
echo "arch-build: bin/$arch/kernel.img ($(stat -c%s "bin/$arch/kernel.img") bytes)"
