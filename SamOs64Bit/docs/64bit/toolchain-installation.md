# SamOs Module 1 (64-bit) - toolchain installation

The 64-bit Module 1 port lives on the `module1-64bit` branch. It
needs a **second** cross compiler targeting `x86_64-elf`, on top
of the i686-elf one that the 32-bit `main` branch already uses.

Verified working set on the box this branch was developed on:

| Tool | Version | Why |
|---|---|---|
| `x86_64-elf-gcc` | 10.2.0 | freestanding 64-bit C compiler - needed from Lecture 8 onward when `kernel.c` rejoins the build |
| `x86_64-elf-ld` | GNU Binutils 2.35 | linker for `kernel.bin`; uses `OUTPUT_FORMAT(binary)` so the bit-width doesn't matter for the final flat image, but the input `.o` files have to be `elf64-x86-64` for the relocatable link |
| `nasm` | 2.16.01 | same as the 32-bit project, but Lecture 7 onward we invoke it with `-f elf64` |
| `qemu-system-x86_64` | 8.2.2 | the test box; `-accel tcg` is the slow-but-portable emulation; KVM also works on Linux hosts |

The 32-bit and 64-bit toolchains live side-by-side under the same
`$HOME/opt/cross` prefix - they install as `i686-elf-*` and
`x86_64-elf-*` so they don't collide. One `PATH` entry covers
both.

If you haven't done the 32-bit toolchain install yet, do that
first in `docs/toolchain-installation.md` - most of the steps
(apt packages, prefix layout, gotchas) are identical, and we
inherit the math libs and `--disable-werror` etc.

---

## 1. Apt packages - same as the 32-bit recipe

If you already followed `docs/toolchain-installation.md`, skip
this. Otherwise:

```bash
sudo apt update
sudo apt install -y \
    build-essential bison flex texinfo \
    libgmp3-dev libmpc-dev libmpfr-dev \
    libcloog-isl-dev libisl-dev \
    nasm qemu-system-x86 mtools xxd
```

---

## 2. x86_64-elf cross compiler (30 - 60 minutes, one-time)

Same canonical recipe as the i686-elf side, with `TARGET` flipped.

### 2.1 Setup

```bash
export PREFIX="$HOME/opt/cross"
export TARGET=x86_64-elf
export PATH="$PREFIX/bin:$PATH"
export BINUTILS_VERSION=2.35
export GCC_VERSION=10.2.0
mkdir -p ~/src/cross && cd ~/src/cross
```

### 2.2 Download (only if you didn't already do the 32-bit build)

```bash
[ -d binutils-$BINUTILS_VERSION ] || {
    wget https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VERSION.tar.xz
    tar -xf binutils-$BINUTILS_VERSION.tar.xz
}
[ -d gcc-$GCC_VERSION ] || {
    wget https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VERSION/gcc-$GCC_VERSION.tar.xz
    tar -xf gcc-$GCC_VERSION.tar.xz
}
```

### 2.3 Build binutils for x86_64-elf

```bash
mkdir -p build-binutils-x86_64 && cd build-binutils-x86_64
../binutils-$BINUTILS_VERSION/configure \
    --target=$TARGET \
    --prefix="$PREFIX" \
    --with-sysroot \
    --disable-nls \
    --disable-werror
make -j"$(nproc)"
make install
cd ..
```

### 2.4 Build gcc for x86_64-elf

```bash
mkdir -p build-gcc-x86_64 && cd build-gcc-x86_64
../gcc-$GCC_VERSION/configure \
    --target=$TARGET \
    --prefix="$PREFIX" \
    --disable-nls \
    --enable-languages=c,c++ \
    --without-headers
make -j"$(nproc)" all-gcc
make -j"$(nproc)" all-target-libgcc
make install-gcc
make install-target-libgcc
cd ..
```

Same flags as the 32-bit build. `--without-headers` is what makes
gcc happy to build a freestanding cross that has no glibc /
sysroot.

### 2.5 Sanity check

```bash
$TARGET-gcc --version | head -1
# expect: x86_64-elf-gcc (GCC) 10.2.0
$TARGET-ld  --version | head -1
# expect: GNU ld (GNU Binutils) 2.35
echo 'int main(void){return 0;}' > /tmp/t.c
$TARGET-gcc -ffreestanding -nostdlib -O2 -c /tmp/t.c -o /tmp/t.o
$TARGET-objdump -h /tmp/t.o | grep 'file format'
# expect: file format elf64-x86-64
```

---

## 3. Verify by building Lecture 7

```bash
git checkout module1-64bit
bash build.sh
# expect: build: bin/os.bin (16794112 bytes)
```

Run the Lecture 7 regression:

```bash
bash tests64/L07-long-mode-entry.sh
echo $?     # 0 = passed
```

What it actually asserts:

| Check | Why |
|---|---|
| `CS = 0x0018` | the far jump into the 64-bit code segment succeeded |
| QEMU tags CS as `CS64` | the descriptor's L-bit is set, CPU is in 64-bit decoding |
| `CR0` bit 31 = 1 | paging on |
| `CR4` bit 5 = 1 | PAE on |
| `EFER` bit 10 = 1 (LMA) | CPU set Long Mode Active itself once PAE + LME + PG were all in place |

---

## 4. About the build flags

The 64-bit Makefile passes `x86_64-elf-gcc` with the same
freestanding-flavoured flags the 32-bit Makefile used, plus the
implicit AMD64 ABI:

```
-g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs
-fno-builtin -O0 -Wall -Werror
```

What we **don't** add (and why):

- `-m64` - `x86_64-elf-gcc` produces 64-bit code by default; the
  flag is redundant
- `-mcmodel=kernel` - sets the code model to "kernel" (sign-
  extended 32-bit immediates instead of full 64-bit). We'll add
  it when we relink the kernel above the 4 GiB boundary
- `-mno-red-zone` - disable the AMD64 red zone (the 128 bytes
  below RSP that callee may use). Same as the code-model concern:
  we'll add it when we start handling interrupts in long mode
  (so the IRQ entry doesn't trample what the user-level callee
  was relying on)
- `-mno-sse`, `-mno-mmx`, `-mno-avx` - disable instruction sets
  that would be a CPL-3-only optimisation; same story, we'll add
  these when interrupt entry needs to be predictable

These flags will get added during Lectures 8 / 33 / 35 as the C
side and the interrupt handlers come back online. Lecture 7 has
no C in the build so none of them apply yet.

---

## 5. Common errors specific to the 64-bit branch

| Symptom | Likely cause | Fix |
|---|---|---|
| `x86_64-elf-gcc: command not found` | only the 32-bit cross is installed | follow section 2 here |
| `relocation truncated to fit: R_X86_64_PC32 against ...` | code model mismatch - we link above the 4 GiB boundary without `-mcmodel=kernel` | not an issue at Lecture 7 (kernel stays under 4 GiB); revisit at the 64-bit kernel relink |
| QEMU registers show `CS = 0x0008` after the supposed iretd, not `0x0018` | the far jump source didn't actually transition; PAE + LME + PG checks haven't all run | re-read section 1 of `docs/64bit/chapters/L07-long-mode-bootloader.md` |
| `nasm: -f elf64 is invalid output format` | nasm is older than 2.10 | `sudo apt install nasm` (Ubuntu 24.04 ships 2.16, fine) |

---

## 6. See also

- `docs/toolchain-installation.md` - the i686-elf side; identical
  recipe with `TARGET=i686-elf`
- `docs/64bit/chapters/L07-long-mode-bootloader.md` - the
  theory + observed-register-values for the first commit on this
  branch
- `tests64/L07-long-mode-entry.sh` - the regression test that
  proves the toolchain produced something the CPU likes
