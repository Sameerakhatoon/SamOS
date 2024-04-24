#!/bin/bash
# efi-build.sh - build the UEFI loader (top-level SamOs.c/.inf/.uni)
# with EDK2 for one architecture.
#
#   bash efi-build.sh X64 bin/SamOs.efi          # BOOTX64.EFI (build.sh)
#   bash efi-build.sh AARCH64 bin/SamOsAA64.efi  # BOOTAA64.EFI
#
# The module is built through ~/edk2's MdeModulePkg: a symlink
# MdeModulePkg/Application/SamOs -> this tree and one line in
# MdeModulePkg.dsc. AARCH64 uses the Ubuntu cross compiler
# (GCC_AARCH64_PREFIX, default aarch64-linux-gnu-).
#
# Environment:
#   EDK2_ROOT       EDK2 checkout (default ~/edk2)
#   EDK2_TOOLCHAIN  EDK2 toolchain tag (default GCC)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
arch="${1:-}"
out="${2:-}"
case "$arch" in
X64|AARCH64) ;;
*)
    echo "usage: $0 X64|AARCH64 OUTPUT.efi" >&2
    exit 2
    ;;
esac
[ -n "$out" ] || { echo "usage: $0 X64|AARCH64 OUTPUT.efi" >&2; exit 2; }
case "$out" in /*) ;; *) out="$PWD/$out" ;; esac

: "${EDK2_TOOLCHAIN:=GCC}"
: "${EDK2_ROOT:=$HOME/edk2}"
: "${GCC_AARCH64_PREFIX:=aarch64-linux-gnu-}"
export GCC_AARCH64_PREFIX

if [ ! -x "$EDK2_ROOT/BaseTools/Source/C/bin/GenFw" ]; then
    echo "efi-build.sh: EDK2 with built BaseTools not found at $EDK2_ROOT" >&2
    exit 2
fi

# ~/edk2 is shared by every worktree, so serialise the symlink swap +
# build + copy.
(
    flock 9
    link="$EDK2_ROOT/MdeModulePkg/Application/SamOs"
    if [ -e "$link" ] && [ ! -L "$link" ]; then
        echo "efi-build.sh: $link exists and is not a symlink" >&2
        exit 1
    fi
    ln -sfn "$ROOT" "$link"
    dsc="$EDK2_ROOT/MdeModulePkg/MdeModulePkg.dsc"
    if ! grep -q 'Application/SamOs/SamOs.inf' "$dsc"; then
        sed -i 's|^\(\s*\)MdeModulePkg/Application/HelloWorld/HelloWorld.inf|&\n\1MdeModulePkg/Application/SamOs/SamOs.inf|' "$dsc"
    fi
    # The module's build dir is shared too, and EDK2 rebuilds only
    # by mtime: another worktree's newer object (from an older
    # SamOs.c) would win over this tree's source. Start clean.
    rm -rf "$EDK2_ROOT/Build/MdeModule/DEBUG_${EDK2_TOOLCHAIN}/$arch/MdeModulePkg/Application/SamOs"
    cd "$EDK2_ROOT"
    export EDK_TOOLS_PATH="$EDK2_ROOT/BaseTools"
    set +u
    . ./edksetup.sh BaseTools >/dev/null
    set -u
    build -p MdeModulePkg/MdeModulePkg.dsc \
          -m MdeModulePkg/Application/SamOs/SamOs.inf \
          -a "$arch" -t "$EDK2_TOOLCHAIN" -q
    mkdir -p "$(dirname "$out")"
    cp "$EDK2_ROOT/Build/MdeModule/DEBUG_${EDK2_TOOLCHAIN}/$arch/SamOs.efi" "$out"
) 9>"$HOME/.samos-edk2.lock"
