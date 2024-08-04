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
├── efi-build.sh           # builds the UEFI loader (X64 or AARCH64) in ~/edk2
├── SamOs.c/.inf/.uni      # the UEFI loader (EDK2 module)
├── docs/                  # GUIDE (build, run, test), locking
└── SamOs64Bit/
    ├── src/               # the kernel
    ├── programs/          # user space: stdlib, shell, blank (the e2e test program)
    ├── tests64/           # run-all.sh: e2e/ (QEMU boots), host/, arch/ (ports), storage/
    └── docs/              # toolchain, testing and GDB guides
```

The original 32-bit kernel from the first half of the book is frozen at the local git tag `samos-32-final`.

## Build and run

The full guide, with every dependency, the cross compilers, building for
x86-64, ARM64 and RISC-V, the Raspberry Pi and VisionFive 2 images, the
QEMU command lines per machine, the tests and their flags, debugging and
troubleshooting, is [docs/GUIDE.md](docs/GUIDE.md). In short, on Ubuntu
24.04 (native or WSL2):

```bash
bash bootstrap.sh                                  # apt packages, x86_64-elf gcc, EDK2
sudo apt install gdisk qemu-efi-aarch64 gdb-multiarch   # not in bootstrap.sh's list
bash build.sh                                      # bin/os.img (x86-64)
bash run.sh -snapshot                              # boot it under OVMF; COM1 on this terminal
bash SamOs64Bit/arch-build.sh aarch64              # SamOs64Bit/bin/aarch64/ (riscv64 likewise)
bash SamOs64Bit/tests64/run-all.sh -j 3            # e2e on all three, host and arch tests
```

## Status

The kernel runs on x86-64, ARM64 and RISC-V in QEMU. For real
hardware (PCs, the Raspberry Pi and the VisionFive 2), see section 11
of [docs/GUIDE.md](docs/GUIDE.md).
