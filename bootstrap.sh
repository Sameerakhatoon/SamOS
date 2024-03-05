#!/bin/bash
# bootstrap.sh - one-shot installer for everything build.sh and
# tests64/e2e/run-all.sh need to start from a fresh Debian/Ubuntu.
#
# Three layers:
#   1. apt packages (system tools: gcc, nasm, qemu, ovmf, mtools, parted, ...)
#   2. x86_64-elf cross compiler built from source into $HOME/opt/cross/
#      (binutils + gcc, target=x86_64-elf). Vol 1 L21 recipe.
#   3. EDK2 + BaseTools at $HOME/edk2/, with this project symlinked
#      under MdeModulePkg/Application/SamOs/ for the UEFI build.
#
# Layers are idempotent: each one no-ops if the artefact is already
# present. Re-run as often as you like.
#
# Tested on Ubuntu 24.04. Should work on Debian 12+. Older distros may
# need a different gcc-NN package and EDK2 toolchain tag.

set -e

CYAN='\033[36m'; GREEN='\033[32m'; YELLOW='\033[33m'; RED='\033[31m'; RESET='\033[0m'
step()   { printf "${CYAN}==> %s${RESET}\n" "$*"; }
ok()     { printf "${GREEN}    OK${RESET} %s\n" "$*"; }
skip()   { printf "${YELLOW}    skip${RESET} %s\n" "$*"; }
warn()   { printf "${YELLOW}    warn${RESET} %s\n" "$*"; }
die()    { printf "${RED}FAIL${RESET} %s\n" "$*" >&2; exit 1; }

PREFIX="${PREFIX:-$HOME/opt/cross}"
TARGET="${TARGET:-x86_64-elf}"
EDK2_ROOT="${EDK2_ROOT:-$HOME/edk2}"
BINUTILS_VER="${BINUTILS_VER:-2.42}"
GCC_VER="${GCC_VER:-13.2.0}"
SAMOS_ROOT="$(cd "$(dirname "$0")" && pwd)"

# -------------------------------------------------------------------
# Layer 1: apt packages
# -------------------------------------------------------------------
step "Layer 1: apt packages"

if ! command -v apt >/dev/null 2>&1; then
    warn "apt not found; skipping (this is not a Debian/Ubuntu system)"
else
    APT_PKGS=(
        build-essential
        nasm
        qemu-system-x86
        ovmf
        mtools
        parted
        dosfstools
        git
        wget
        # Cross-compiler build prerequisites:
        bison
        flex
        libgmp3-dev
        libmpc-dev
        libmpfr-dev
        texinfo
        libisl-dev
        # EDK2 build prerequisites:
        uuid-dev
        iasl
        python3
        python3-distutils-extra
    )
    MISSING=()
    for p in "${APT_PKGS[@]}"; do
        if ! dpkg -s "$p" >/dev/null 2>&1; then
            MISSING+=("$p")
        fi
    done

    if [ ${#MISSING[@]} -eq 0 ]; then
        ok "all apt packages already installed (${#APT_PKGS[@]} total)"
    else
        step "Installing ${#MISSING[@]} missing packages: ${MISSING[*]}"
        sudo apt update
        sudo apt install -y "${MISSING[@]}"
        ok "apt install done"
    fi
fi

# -------------------------------------------------------------------
# Layer 2: x86_64-elf cross compiler
# -------------------------------------------------------------------
step "Layer 2: x86_64-elf cross compiler (binutils $BINUTILS_VER + gcc $GCC_VER)"

if [ -x "$PREFIX/bin/${TARGET}-gcc" ] && [ -x "$PREFIX/bin/${TARGET}-ld" ]; then
    GCC_FOUND_VER=$("$PREFIX/bin/${TARGET}-gcc" --version | head -1 || true)
    ok "cross compiler already present at $PREFIX/bin/  ($GCC_FOUND_VER)"
else
    step "Building cross compiler (this takes 20-40 minutes on a typical laptop)"

    BUILD_DIR="$HOME/src/cross-build"
    mkdir -p "$BUILD_DIR" "$PREFIX"
    cd "$BUILD_DIR"

    # ---- binutils ----
    if [ ! -d "binutils-${BINUTILS_VER}" ]; then
        step "downloading binutils-${BINUTILS_VER}"
        wget -q "https://ftp.gnu.org/gnu/binutils/binutils-${BINUTILS_VER}.tar.gz"
        tar -xf "binutils-${BINUTILS_VER}.tar.gz"
    fi
    rm -rf "binutils-build"; mkdir -p binutils-build; cd binutils-build
    step "configuring binutils"
    "../binutils-${BINUTILS_VER}/configure" \
        --target="$TARGET" \
        --prefix="$PREFIX" \
        --with-sysroot \
        --disable-nls \
        --disable-werror
    step "building binutils"
    make -j"$(nproc)"
    make install
    cd "$BUILD_DIR"
    ok "binutils installed"

    # ---- gcc ----
    if [ ! -d "gcc-${GCC_VER}" ]; then
        step "downloading gcc-${GCC_VER}"
        wget -q "https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VER}/gcc-${GCC_VER}.tar.gz"
        tar -xf "gcc-${GCC_VER}.tar.gz"
    fi
    rm -rf "gcc-build"; mkdir -p gcc-build; cd gcc-build
    step "configuring gcc"
    export PATH="$PREFIX/bin:$PATH"
    "../gcc-${GCC_VER}/configure" \
        --target="$TARGET" \
        --prefix="$PREFIX" \
        --disable-nls \
        --enable-languages=c \
        --without-headers
    step "building gcc (--all-gcc)"
    make -j"$(nproc)" all-gcc
    step "building libgcc (--all-target-libgcc)"
    make -j"$(nproc)" all-target-libgcc
    make install-gcc
    make install-target-libgcc
    cd "$SAMOS_ROOT"
    ok "gcc installed to $PREFIX/bin/${TARGET}-gcc"
fi

# -------------------------------------------------------------------
# Layer 3: EDK2 + BaseTools + module symlink
# -------------------------------------------------------------------
step "Layer 3: EDK2 ($EDK2_ROOT) + SamOs module symlink"

if [ ! -d "$EDK2_ROOT" ]; then
    step "cloning EDK2 into $EDK2_ROOT (this is large, ~2 GB)"
    git clone --depth 1 --recurse-submodules https://github.com/tianocore/edk2.git "$EDK2_ROOT"
    ok "EDK2 cloned"
else
    ok "EDK2 already present at $EDK2_ROOT"
fi

# BaseTools "built" = the C binaries exist in Source/C/bin AND the
# PosixLike wrapper is in place. The wrapper alone is shipped in the
# git repo; the C binaries are produced by `make -C BaseTools`.
if [ -x "$EDK2_ROOT/BaseTools/Source/C/bin/GenFw" ]; then
    ok "BaseTools already built (Source/C/bin populated)"
else
    step "building EDK2 BaseTools"
    cd "$EDK2_ROOT"
    make -C BaseTools -j"$(nproc)"
    cd "$SAMOS_ROOT"
    ok "BaseTools built"
fi

SAMOS_LINK="$EDK2_ROOT/MdeModulePkg/Application/SamOs"
if [ ! -e "$SAMOS_LINK" ]; then
    step "linking $SAMOS_ROOT -> $SAMOS_LINK"
    mkdir -p "$(dirname "$SAMOS_LINK")"
    ln -s "$SAMOS_ROOT" "$SAMOS_LINK"
    ok "module linked into EDK2 tree"
elif [ -L "$SAMOS_LINK" ]; then
    ok "module already linked"
else
    warn "$SAMOS_LINK exists and is not a symlink; leaving it alone"
fi

# -------------------------------------------------------------------
# Final report
# -------------------------------------------------------------------
echo
printf "${GREEN}==> bootstrap complete${RESET}\n"
printf "    cross compiler : %s\n" "$PREFIX/bin/${TARGET}-gcc"
printf "    EDK2 root      : %s\n" "$EDK2_ROOT"
printf "    samos project  : %s\n" "$SAMOS_ROOT"
echo
echo "Next steps:"
echo "    cd $SAMOS_ROOT"
echo "    bash build.sh                                  # build kernel + UEFI + disk image"
echo "    bash SamOs64Bit/tests64/e2e/run-all.sh         # run all 122 e2e tests"
