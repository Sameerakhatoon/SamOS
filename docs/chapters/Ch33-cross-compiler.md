# Ch 29 - Creating a Cross Compiler so we can code in C

**Book pages:** 70-72 (Part 5)
**Code added:** nothing in repo; relies on `$HOME/opt/cross/bin/i686-elf-*`
**Test:** `tests/07-cross-compiler-works.sh`

## Why we need a cross compiler

The host `gcc` is configured for the host OS (Linux). By default it:

- Links against glibc, which expects Linux system calls like `read`, `write`, `mmap`.
- Emits ELF that assumes a dynamic linker is present.
- Defines macros like `__linux__` and pulls in headers that assume POSIX is around.

None of that is true inside our kernel. We need a compiler that builds for a **bare-metal target**, with no runtime support assumed. The convention is to build a GCC variant tagged `i686-elf` (or `x86_64-elf` later for 64-bit). The `-elf` suffix means "produce ELF object files for a freestanding x86 target, no OS underneath".

## What is on this host

```
$HOME/opt/cross/bin/i686-elf-gcc       -> 10.2.0
$HOME/opt/cross/bin/i686-elf-as        -> GNU Binutils 2.35
$HOME/opt/cross/bin/i686-elf-ld        -> GNU Binutils 2.35
$HOME/opt/cross/bin/i686-elf-readelf
$HOME/opt/cross/bin/i686-elf-objdump
$HOME/opt/cross/bin/i686-elf-nm
... etc.
```

Versions match the book (Binutils 2.35, GCC 10.2.0). The toolchain was prepared ahead of time so chapters from here on can rely on it.

## The book's build procedure

For reference, in case we ever need to rebuild it from scratch:

```bash
sudo apt install build-essential bison flex libgmp3-dev libmpc-dev \
                 libmpfr-dev libcloog-isl-dev libisl-dev texinfo

# Download:
#   binutils-2.35.tar.xz from https://ftp.gnu.org/gnu/binutils
#   gcc-10.2.0.tar.gz    from https://ftp.lip6.fr/pub/gcc/releases/gcc-10.2.0
# Extract both into ~/src/.

export PREFIX="$HOME/opt/cross"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"

cd ~/src
mkdir build-binutils && cd build-binutils
../binutils-2.35/configure --target=$TARGET --prefix="$PREFIX" \
    --with-sysroot --disable-nls --disable-werror
make
make install

cd ~/src
mkdir build-gcc && cd build-gcc
../gcc-10.2.0/configure --target=$TARGET --prefix="$PREFIX" \
    --disable-nls --enable-languages=c,c++ --without-headers
make all-gcc
make all-target-libgcc
make install-gcc
make install-target-libgcc

$HOME/opt/cross/bin/i686-elf-gcc --version
```

Build takes 30 to 60 minutes depending on the machine. We are not running it now because the artifacts already exist.

## How we use it in tests

The test compiles a freestanding C file with:

```bash
i686-elf-gcc -ffreestanding -nostdlib -nostartfiles -c hello.c -o hello.o
```

Then verifies via `i686-elf-readelf -h` that the output is an ELF for Intel 80386, and via `i686-elf-nm` that the function symbol is present. If either fails the toolchain is broken or missing.

## Where it lands next

Ch 30 onwards starts treating the kernel as `boot.asm` (loader) plus a C kernel that the loader hands control to. The Makefile grows rules to compile `.c` files through `i686-elf-gcc` and link them into `kernel.bin`. The boot sector reads more sectors off disk to pull the kernel into memory, then far-jumps into the kernel's entry point.
