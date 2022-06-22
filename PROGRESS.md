# SamOs progress

Walking **"Developing A Multi-Threaded Kernel From Scratch"** by Daniel McCarthy (Dragon Zap, 861 pages).


Use `git log --oneline` to map checkboxes to commits.

Book source: `D:\kernel_project\kernel-md\kernel.md` (page-anchored).

---

## Setup

- [x] Cross-toolchain confirmed (`i686-elf-gcc`, `nasm`, `qemu-system-x86_64`, `gdb`) - `tests/01-tooling-installed.sh`
- [x] `bin/` and `build/` directories exist
- [x] `.gitignore` covers build artifacts
- [x] `tests/run.sh` harness in place

## Part 1 - Introduction & Requirements

- [x] **Ch 1** - Introduction (prose) → [Ch01.md](docs/chapters/Ch01.md)
- [x] **Ch 2** - Requirements (prose) → [Ch02.md](docs/chapters/Ch02.md)
- [x] **Ch 3** - Who This Book Is For (prose) → [Ch03.md](docs/chapters/Ch03.md)

## Part 2 - The Basics

- [x] **Ch - What is Memory?** (prose) → [Ch04-what-is-memory.md](docs/chapters/Ch04-what-is-memory.md)
- [x] **Ch - Memory Management & Virtual Memory** (prose) → [Ch05-memory-management.md](docs/chapters/Ch05-memory-management.md)
- [x] **Ch 4** - The Boot Process (prose) → [Ch06-boot-process.md](docs/chapters/Ch06-boot-process.md)

## Part 3 - Tooling

- [x] **Ch - NASM, GCC, QEMU, GDB install** → [Ch07-tooling.md](docs/chapters/Ch07-tooling.md) - test `tests/01-tooling-installed.sh`

## Part 4 - Real Mode Development

- [x] **Ch 5** - Project setup (`boot.asm` placeholder) → [Ch08-project-setup.md](docs/chapters/Ch08-project-setup.md)
- [x] **Ch - The Hello World Bootloader** - `org 0x7c00`, BIOS int 0x10, boot signature 0xAA55 → [Ch09-hello-world-bootloader.md](docs/chapters/Ch09-hello-world-bootloader.md) - test `tests/02-bootloader-prints-hello.sh`
- [x] **Ch - Compile the bootloader** - `nasm -f bin`, verify 512 bytes ending `55 AA` (verified in same test)
- [x] **Ch - Run in QEMU** - verified by `tests/02-bootloader-prints-hello.sh`
- [x] **Ch 6** - Understanding Real Mode (prose) -> [Ch10-understanding-real-mode.md](docs/chapters/Ch10-understanding-real-mode.md)
- [x] **Ch 7** - Segmentation Memory Model (prose) -> [Ch11-segmentation.md](docs/chapters/Ch11-segmentation.md)
- [ ] **Ch 8** - Refining Our Bootloader - `ORG 0`, explicit segments, `cli`/`sti`
- [ ] **Ch 9** - Booting on Real Hardware (prose - staying on QEMU)
- [ ] **Ch 10** - The Interrupt Vector Table (prose)
- [ ] **Ch 11** - Implementing Our Own Interrupts in Real Mode
- [ ] **Ch 13** - Exception Handling Using Interrupts
- [ ] **Ch 14** - Disk Access and How It Works (prose)
- [ ] **Ch 15** - A Bootloader That Reads From Disk
- [ ] **Ch 16** - Master Boot Record (prose)
- [ ] **Ch 17** - MBR Deeper Understanding (prose)
- [ ] **Ch 18** - Summary of Real Mode (prose)

## Part 5 - Protected Mode Development

> Filled in as we read each chapter. Anchors: A20 line, GDT, 32-bit transition, paging, IDT, ATA PIO, FAT16, ELF, multitasking, syscalls, keyboard, shell.

## Part 6 - Graphics, Windowing & Drivers

> Filled in once we reach it. Anchors: framebuffer, mouse, window events, PCI, PATA, NVMe, sleep syscall.

---

## Gotchas log

Each entry: short title + chapter where surfaced + link to the gotcha note.

> None yet - first gotcha gets logged as it surfaces.

---
