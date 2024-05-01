#!/usr/bin/env bash
# Host-side unit tests for code that does not need QEMU:
# the userland libc (printf/snprintf, strtol, string helpers,
# calloc, itoa_r), the shell parser and the line editor. The aarch64 port adds
# the device tree parser (fdt_test.c, at the end).
#
# Builds programs/stdlib/src/*.c with the host gcc, renames every
# symbol to sam_<name> with objcopy so they cannot collide with
# glibc, and links tests64/host/libc_test.c against them.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT

CFLAGS="-ffreestanding -fno-builtin -fno-stack-protector -O0 -std=gnu99 -Wall -Werror -Wno-unused-parameter -I$root/programs/stdlib/src"
objs=()
for src in stdio stdlib string memory samos; do
    gcc $CFLAGS -c "$root/programs/stdlib/src/$src.c" -o "$out/$src.o"
    objcopy --prefix-symbols=sam_ "$out/$src.o"
    objs+=("$out/$src.o")
done
gcc -O0 -g -Wall -Wno-format-zero-length -o "$out/libc_test" "$here/libc_test.c" "${objs[@]}"
"$out/libc_test"

# Keyboard translator: classic.c against stubs for io/idt/keyboard.c.
gcc -ffreestanding -fno-builtin -O0 -std=gnu99 -Wall -Werror -Wno-unused-parameter \
    -I"$root/src" -c "$root/src/keyboard/classic.c" -o "$out/classic.o"
gcc -O0 -g -Wall -Wno-format-security -I"$root/src" -o "$out/keyboard_test" \
    "$here/keyboard_test.c" "$out/classic.o"
"$out/keyboard_test"

# Device tree parser. Built freestanding first to prove it
# needs nothing from libc (no undefined symbols), then linked into
# the test with the sanitizers, against QEMU's own aarch64 and
# riscv64 trees in fixtures/.
gcc -ffreestanding -fno-builtin -O2 -std=gnu99 -Wall -Wextra -Werror -I"$root/src" \
    -c "$root/src/firmware/fdt.c" -o "$out/fdt_free.o"
if nm -u "$out/fdt_free.o" | grep . > /dev/null; then
    echo "FAIL: fdt.c needs external symbols:"; nm -u "$out/fdt_free.o"; exit 1
fi
gcc -O1 -g -std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
    -I"$root/src" -o "$out/fdt_test" "$here/fdt_test.c" "$root/src/firmware/fdt.c"
"$out/fdt_test" "$here/fixtures"

# The hardware layer's done line and its structural checks.
bash "$here/hal_check.sh"

# virtio core and virtio-mmio transport against a fake MMIO block
# device (x86 QEMU has no virtio-mmio), plus the virtio-input key map.
vflags="-std=gnu99 -fno-builtin -Wall -Wextra -Werror -Wno-unused-parameter -DVIRTIO_HOST_TEST -I$root/src"
gcc -O1 -g $vflags -fsanitize=address,undefined -fno-sanitize-recover=all -o "$out/virtio_test" \
    "$here/virtio_test.c" "$root/src/virtio/virtio.c" "$root/src/virtio/virtio_mmio.c" \
    "$root/src/keyboard/virtio_input.c"
"$out/virtio_test"
# The transport-independent part builds freestanding for the two other
# ports (no x86 instruction, no port I/O).
for cc in aarch64-linux-gnu-gcc riscv64-linux-gnu-gcc; do
    command -v "$cc" >/dev/null 2>&1 || continue
    for f in virtio virtio_mmio; do
        "$cc" -ffreestanding -O2 $vflags -c "$root/src/virtio/$f.c" -o "$out/$f-$cc.o"
    done
    echo "virtio: core and mmio transport build with $cc"
done
