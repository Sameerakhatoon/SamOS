# SamOs

A 64-bit multi-threaded kernel built from scratch, following Daniel McCarthy's **"Developing A Multi-Threaded Kernel From Scratch"** (Dragon Zap), and then fixed and extended past the book.

It boots through UEFI: a small EDK2 loader (`SamOs.efi`) reads `KERNEL.BIN` from the EFI system partition, exits boot services and jumps to the kernel at physical `0x100000`. The kernel draws to the GOP framebuffer and mirrors its console to the COM1 serial port.

## How this repo is organized

```
SamOS/
├── README.md
├── build.sh               # one-command build: bin/os.img (no sudo)
├── run.sh                 # boot bin/os.img in QEMU + OVMF
├── bootstrap.sh           # installs the toolchain (apt, x86_64-elf gcc, EDK2)
├── SamOs.c/.inf/.uni      # the UEFI loader (EDK2 module)
└── SamOs64Bit/
    ├── src/               # the kernel
    ├── programs/          # user space: stdlib, shell, blank (the e2e test program)
    ├── tests64/           # run-all.sh + e2e/ (QEMU boots, marker + serial assertions)
    └── docs/              # toolchain, testing and GDB guides
```

The original 32-bit kernel from the first half of the book is frozen at the local git tag `samos-32-final`.

## Build and run

Toolchain: the `x86_64-elf` GCC cross-compiler in `$HOME/opt/cross/`, `nasm`, EDK2 at `$HOME/edk2` (BaseTools built), `sgdisk`, `mtools`, `dosfstools`, QEMU and OVMF. `./bootstrap.sh` installs all of them.

```bash
./build.sh                              # builds bin/os.img
./run.sh                                # boots it; serial console on stdio
bash SamOs64Bit/tests64/run-all.sh      # e2e suite, prints "N passed, M failed"
```

## Status

The kernel runs on x86-64 under UEFI. ARM64 and RISC-V ports are
planned.
