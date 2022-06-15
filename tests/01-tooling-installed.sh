#!/bin/bash
# tests/01-tooling-installed.sh
#
# Verifies the four book-required tools (nasm, gcc, qemu-system-x86_64, gdb)
# are present on this host. Plus the i686-elf cross-compiler at $HOME/opt/cross/
# (SamOs pre-stages it because later chapters need it; the book mentions it
# explicitly around the kernel-loader chapter).
#
# Pass: each tool prints a version. Fail: any tool missing.

set -e

check() {
    local tool="$1"
    local cmd="$2"
    if ! out=$(eval "$cmd" 2>&1); then
        echo "FAIL: $tool - command '$cmd' failed: $out"
        exit 1
    fi
    if [ -z "$out" ]; then
        echo "FAIL: $tool - no output from '$cmd'"
        exit 1
    fi
}

check "nasm"                "nasm -v"
check "gcc"                 "gcc --version | head -1"
check "qemu-system-x86_64"  "qemu-system-x86_64 --version | head -1"
check "gdb"                 "gdb --version | head -1"

# Cross-compiler - required from Ch ~28 onward when we build a freestanding
# 32-bit kernel that can't be linked against the host glibc.
if [ -x "$HOME/opt/cross/bin/i686-elf-gcc" ]; then
    "$HOME/opt/cross/bin/i686-elf-gcc" --version > /dev/null
else
    echo "WARN: $HOME/opt/cross/bin/i686-elf-gcc not found yet - needed by Ch 28+"
fi

exit 0
