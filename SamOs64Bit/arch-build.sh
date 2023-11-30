#!/bin/bash
# arch-build.sh - build a non-x86 SamOS kernel (the first aarch64 and riscv64 spikes).
#
#   bash arch-build.sh aarch64      # -> bin/aarch64/kernel.img
#   bash arch-build.sh riscv64      # -> bin/riscv64/kernel.img
#
# Separate from build.sh and the Makefile on purpose: the x86 build
# and its outputs are untouched until the hardware layer adds `make ARCH=...`.
# Objects and kernel.elf go to build/<arch>/, which `make clean` does
# not touch.
#
# Toolchain: the Ubuntu cross compilers <arch>-linux-gnu-gcc, used
# freestanding (no libc, no crt, own linker script). The plan named
# the <arch>-elf-gcc ones; the Linux triplets produce the same code
# with these flags, so they stand in until bootstrap.sh builds them.
set -euo pipefail
cd "$(dirname "$0")"

arch="${1:-}"
case "$arch" in
aarch64)
    CROSS="${CROSS:-aarch64-linux-gnu-}"
    # -mgeneral-regs-only: no FP/SIMD in the kernel (no FP context to
    #   save yet, and gcc would otherwise use q registers for copies).
    # -mstrict-align: the MMU is off, so all memory is Device memory
    #   and an unaligned access faults.
    ARCH_CFLAGS="-mgeneral-regs-only -mstrict-align -mcmodel=small"
    # One RWX segment until the aarch64 MMU work brings the MMU and W^X.
    ARCH_LDFLAGS="-Wl,--no-warn-rwx-segments"
    SRCS="src/arch/aarch64/boot.S src/arch/aarch64/vectors.S src/arch/aarch64/main.c
          src/arch/aarch64/pl011.c src/firmware/fdt.c
          src/arch/aarch64/console.c src/arch/aarch64/trap.c src/arch/aarch64/gicv3.c
          src/arch/aarch64/timer.c src/arch/aarch64/sched.c src/arch/aarch64/fault_test.S"
    ;;
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
    echo "usage: $0 aarch64|riscv64" >&2
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
