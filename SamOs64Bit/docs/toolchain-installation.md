# SamOs - toolchain installation

What you need on a fresh Ubuntu 24.04 (or any modern Debian-family)
host to build, run, and test SamOs end-to-end.

Verified working set (the exact versions this repo's tests are
green against):

| Tool | Version we use | Why |
|---|---|---|
| `i686-elf-gcc` | 10.2.0 | freestanding 32-bit C compiler for the kernel and the user programs (blank.elf, shell.elf, stdlib.elf) |
| `i686-elf-ld`  | GNU Binutils 2.35 | linker; same toolchain bundle as gcc above |
| `nasm` | 2.16.01 | assembler for `boot.asm`, `kernel.asm`, the IDT/GDT/TSS stubs, etc. |
| `qemu-system-x86_64` | 8.2.2 | emulator the test suite uses (`-accel tcg -display none -serial file:...`) |
| `mtools` (`mformat`, `mcopy`) | 4.0.43 | builds the FAT16 disk image without needing `sudo mount` |
| `make`, `gcc`, `xxd`, `dd`, `awk`, `python3` | distro packages | helpers the test suite expects |

The cross compiler is the only non-trivial piece - it has to be
**freestanding** (no glibc, no Linux ABI) and target the right
architecture. The other tools come from apt.

---

## 1. Distro packages (5 minutes)

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    bison \
    flex \
    libgmp3-dev \
    libmpc-dev \
    libmpfr-dev \
    texinfo \
    libcloog-isl-dev \
    libisl-dev \
    nasm \
    qemu-system-x86 \
    mtools \
    xxd
```

The `build-essential` line covers gcc / g++ / make / libc. The
five `libgmp / libmpc / libmpfr / libcloog-isl / libisl` lines are
the math libraries gcc-the-source-build links against.

---

## 2. i686-elf cross compiler (30 - 60 minutes, one-time)

Three pieces: binutils, gcc, then optionally objconv. We mirror
the canonical OS-dev recipe at https://wiki.osdev.org/GCC_Cross-Compiler.

### 2.1 Pick versions and the install path

```bash
export PREFIX="$HOME/opt/cross"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"
export BINUTILS_VERSION=2.35
export GCC_VERSION=10.2.0
mkdir -p ~/src/cross && cd ~/src/cross
```

Both 32-bit (`i686-elf`) and 64-bit (`x86_64-elf`) toolchains
share the same `PREFIX`. They install side-by-side as
`i686-elf-gcc`, `i686-elf-ld`, `x86_64-elf-gcc`, etc., so a single
`PATH` entry covers both.

### 2.2 Download

```bash
wget https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VERSION.tar.xz
wget https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VERSION/gcc-$GCC_VERSION.tar.xz
tar -xf binutils-$BINUTILS_VERSION.tar.xz
tar -xf gcc-$GCC_VERSION.tar.xz
```

### 2.3 Build binutils

```bash
mkdir -p build-binutils-i686 && cd build-binutils-i686
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

Flags decoded:

- `--target=i686-elf` - produce binutils that emit i686 ELF (the
  kernel and user binaries are ELF32 even though the boot sector
  is a 512-byte raw blob)
- `--prefix=$HOME/opt/cross` - install into our user-local
  directory, no sudo needed
- `--with-sysroot` - declare a sysroot, even though we don't
  populate one (we're freestanding); satisfies configure
- `--disable-nls` - drop internationalisation, faster build
- `--disable-werror` - newer host gcc warns on stuff older
  binutils source doesn't expect; without this, the build aborts

### 2.4 Sanity check binutils

```bash
which -a $TARGET-as $TARGET-ld $TARGET-objdump
$TARGET-ld --version | head -1
# expect: GNU ld (GNU Binutils) 2.35
```

### 2.5 Build gcc

```bash
mkdir -p build-gcc-i686 && cd build-gcc-i686
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

- `--enable-languages=c,c++` - we only need C, but C++ comes
  cheap and lets you read libstdc++ for reference later
- `--without-headers` - declares we are freestanding, gcc won't
  try to link or compile glibc-flavoured headers it can't find
- `make all-gcc` then `make all-target-libgcc` is the standard
  two-stage incantation for freestanding cross targets

### 2.6 Sanity check gcc

```bash
$TARGET-gcc --version | head -1
# expect: i686-elf-gcc (GCC) 10.2.0
$TARGET-gcc -ffreestanding -O0 -c -x c /dev/null -o /tmp/t.o
$TARGET-objdump -h /tmp/t.o | grep 'file format'
# expect: file format elf32-i386
```

---

## 3. PATH persistence

Add to your shell init so the cross tools are on the path for
every shell, not just the one that built them:

```bash
echo 'export PATH="$HOME/opt/cross/bin:$PATH"' >> ~/.bashrc
```

`build.sh` also exports this explicitly so the build works even
in a clean shell.

---

## 4. Verify by building SamOs

From the repo root:

```bash
bash build.sh
# expect: build: bin/os.bin (16xxxxx bytes)
qemu-system-x86_64 -hda bin/os.bin -m 256 -accel tcg
# expect VGA window with "Hello world!" then a multitask demo
```

Or, headless, drive the regression suite:

```bash
bash tests/run.sh
# expect: passed: 50   failed: 0
```

---

## 5. Common errors

| Symptom | Likely cause | Fix |
|---|---|---|
| `i686-elf-gcc: command not found` | PATH doesn't include `$HOME/opt/cross/bin` | re-source `.bashrc` or `export PATH=...` manually |
| `binutils: configure: error: cannot find suitable libmpc` | host is missing one of the math libs | re-run the apt install above |
| `gcc: internal compiler error: in process_linker_script` | gcc source too new for the host gcc, OR --disable-werror missing | rerun configure with `--disable-werror` |
| `nasm: error: -f elf is not a valid output format` | nasm too old (we need 2.13+) | upgrade nasm via apt (Ubuntu 24.04 ships 2.16) |
| `qemu-system-x86_64 ... unable to find CPU model 'qemu64'` | wrong qemu package | install `qemu-system-x86` (note: the dash, not `qemu-system-x86_64` as a package name) |

---

## 6. See also

- `docs/64bit/toolchain-installation.md` - the same recipe with
  `TARGET=x86_64-elf` for the 64-bit Module 1 branch
- `docs/testing-guide.md` - how to use the test suite once the
  toolchain is in place
- `docs/feature-walkthrough.md` - driving QEMU by hand after a
  successful build
