#!/bin/bash
# tests/07-cross-compiler-works.sh
#
# Ch 29 - verify the i686-elf cross-compiler at $HOME/opt/cross/bin/ can
# produce a freestanding ELF object from C source. We assemble a trivial
# C file, inspect the output via i686-elf-readelf, and check the target
# machine is 80386 (which is what i686-elf produces).

set -e
cd "$(dirname "$0")/.."

CROSS="$HOME/opt/cross/bin/i686-elf"
if [ ! -x "${CROSS}-gcc" ]; then
    echo "FAIL: ${CROSS}-gcc not found"
    exit 1
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cat > "$tmp/hello.c" <<'EOF'
int sam_value(void){
    return 0x42;
}
EOF

"${CROSS}-gcc" -ffreestanding -nostdlib -nostartfiles -c "$tmp/hello.c" -o "$tmp/hello.o"

# Confirm it is an ELF for i386.
machine=$("${CROSS}-readelf" -h "$tmp/hello.o" | awk -F: '/Machine/ {print $2}' | tr -d ' \t')
if [ "$machine" != "Intel80386" ]; then
    echo "FAIL: hello.o target machine = '$machine', expected Intel80386"
    "${CROSS}-readelf" -h "$tmp/hello.o" | head -15
    exit 1
fi

# Confirm sam_value is in the symbol table.
if ! "${CROSS}-nm" "$tmp/hello.o" | grep -q ' sam_value$'; then
    echo "FAIL: sam_value symbol not in hello.o"
    "${CROSS}-nm" "$tmp/hello.o"
    exit 1
fi

exit 0
