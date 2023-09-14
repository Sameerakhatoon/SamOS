#!/usr/bin/env bash
# Host-side unit tests for code that does not need QEMU:
# the userland libc (printf/snprintf, strtol, string helpers,
# calloc, itoa_r), the shell parser and the line editor.
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
