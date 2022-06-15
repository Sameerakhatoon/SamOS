# Ch - Installing what we need for real mode kernel development

**Book pages:** 10-11 (Part 3 - Tooling)
**Code in this chapter:** none - install verification

## What the book installs

| Tool | Purpose | Ubuntu package |
|------|---------|----------------|
| **NASM** | x86 assembler/disassembler | `nasm` |
| **GCC** | C compiler suite | `build-essential` |
| **QEMU** | x86 system emulator (no reboots needed) | `qemu-system-x86` |
| **GDB** | debugger (works with QEMU's `-s -S` flags) | `gdb` |

## What's on this host

```
nasm                : 2.16.01
gcc                 : 13.3.0
qemu-system-x86_64  : 8.2.2
gdb                 : (installed)
```

Plus the `$HOME/opt/cross/` GCC cross-compiler (`i686-elf-gcc`, `x86_64-elf-gcc`) needed once the book starts producing freestanding ELF kernels - the host gcc emits ELF-with-glibc by default, which won't load as a freestanding kernel. The cross-compiler isn't mentioned by the book at this point but will be needed at Ch ~~28-30; pre-staged so it isn't a surprise later.

## SamOs test

`tests/01-tooling-installed.sh` calls each tool with `--version` (or the closest equivalent) and asserts non-zero output. Run with `./tests/run.sh` from the repo root.
