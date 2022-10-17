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
- [x] **Ch 8** - Refining Our Bootloader. `ORG 0`, explicit segments, `cli`/`sti` -> [Ch12-refining-bootloader.md](docs/chapters/Ch12-refining-bootloader.md)
- [x] **Ch 9** - Booting on Real Hardware (procedure only, not executed) -> [Ch13-real-hardware.md](docs/chapters/Ch13-real-hardware.md)
- [x] **Ch 10** - What Are Interrupts? (prose) -> [Ch14-what-are-interrupts.md](docs/chapters/Ch14-what-are-interrupts.md)
- [x] **Ch 11** - The Interrupt Vector Table Explained (prose) -> [Ch15-ivt.md](docs/chapters/Ch15-ivt.md)
- [x] **Ch 12** - Implementing Our Own Interrupts in Real Mode -> [Ch16-our-own-interrupt.md](docs/chapters/Ch16-our-own-interrupt.md)
- [x] **Ch 13** - Exception Handling Using Interrupts -> [Ch17-exception-handling.md](docs/chapters/Ch17-exception-handling.md)
- [x] **Ch 14** - Disk Access and How It Works (prose) -> [Ch18-disk-access.md](docs/chapters/Ch18-disk-access.md)
- [x] **Ch 15** - A Bootloader That Reads From Disk -> [Ch19-bootloader-reads-disk.md](docs/chapters/Ch19-bootloader-reads-disk.md)
- [x] **Ch 16** - Master Boot Record (prose) -> [Ch20-mbr.md](docs/chapters/Ch20-mbr.md)
- [x] **Ch 17** - MBR Deeper Understanding (prose) -> [Ch21-mbr-deeper.md](docs/chapters/Ch21-mbr-deeper.md)
- [x] **Ch 18** - Summary of Real Mode (prose) -> [Ch22-real-mode-summary.md](docs/chapters/Ch22-real-mode-summary.md)

## Part 5 - Protected Mode Development

- [x] **Ch 19** - What Is Protected Mode? (prose) -> [Ch23-what-is-protected-mode.md](docs/chapters/Ch23-what-is-protected-mode.md)
- [x] **Ch 20** - Advantages of Protected Mode (prose) -> [Ch24-advantages-of-protected-mode.md](docs/chapters/Ch24-advantages-of-protected-mode.md)
- [x] **Ch 21** - Entering Protected Mode (prose) -> [Ch25-entering-protected-mode.md](docs/chapters/Ch25-entering-protected-mode.md)
- [x] **Ch 22** - Switching Our Kernel to Protected Mode (GDT, CR0.PE, far jump to 32-bit) -> [Ch26-switching-to-protected-mode.md](docs/chapters/Ch26-switching-to-protected-mode.md)
- [x] **Ch 23** - Stepping Through the Protected Mode Transition with GDB (procedure) -> [Ch27-gdb-walkthrough.md](docs/chapters/Ch27-gdb-walkthrough.md)
- [x] **Ch 24** - Understanding the Global Descriptor Table (prose) -> [Ch28-understanding-gdt.md](docs/chapters/Ch28-understanding-gdt.md)
- [x] **Ch 25** - Restructuring Our Project (boot.asm into src/boot/) -> [Ch29-restructure-project.md](docs/chapters/Ch29-restructure-project.md)
- [x] **Ch 26** - Automated Building with Makefiles -> [Ch30-makefile.md](docs/chapters/Ch30-makefile.md)
- [x] **Ch 27** - Enabling the A20 Line -> [Ch31-a20-line.md](docs/chapters/Ch31-a20-line.md)
- [x] **Ch 28** - Protection Rings in 32-bit Protected Mode (prose) -> [Ch32-protection-rings.md](docs/chapters/Ch32-protection-rings.md)
- [x] **Ch 29** - Creating a Cross Compiler so we can code in C -> [Ch33-cross-compiler.md](docs/chapters/Ch33-cross-compiler.md)
- [x] **Ch 30** - Understanding ATA Drives (prose) -> [Ch34-understanding-ata.md](docs/chapters/Ch34-understanding-ata.md)
- [x] **Ch 31** - LBA vs CHS (prose) -> [Ch35-lba-vs-chs.md](docs/chapters/Ch35-lba-vs-chs.md)
- [x] **Ch 32** - Extending Our 32-Bit Kernel Across Multiple Sectors (ATA PIO, kernel.asm, linker.ld, Makefile rewrite) -> [Ch36-extending-kernel-multiple-sectors.md](docs/chapters/Ch36-extending-kernel-multiple-sectors.md)
- [x] **Ch 33** - Debugging Your 32-Bit Kernel (procedure) -> [Ch37-debugging-32bit-kernel.md](docs/chapters/Ch37-debugging-32bit-kernel.md)
- [x] **Ch 34** - Improving Cleaning in the Makefile -> [Ch38-improving-cleaning.md](docs/chapters/Ch38-improving-cleaning.md)
- [x] **Ch 35** - Addressing Alignment Issues (linker.ld page-align sections) -> [Ch39-alignment.md](docs/chapters/Ch39-alignment.md)
- [x] **Ch 36** - Transitioning to C (kernel.c + kernel.h, Makefile C rule, kernel.asm calls kernel_main) -> [Ch40-transitioning-to-c.md](docs/chapters/Ch40-transitioning-to-c.md)
- [x] **Ch 37** - Understanding Text Mode in Protected Mode (prose) -> [Ch41-text-mode-protected.md](docs/chapters/Ch41-text-mode-protected.md)
- [x] **Ch 38** - Implementing Text Mode (VGA text driver, prints "Hello world!") -> [Ch42-implementing-text-mode.md](docs/chapters/Ch42-implementing-text-mode.md)
- [x] **Ch 39** - Interrupt Descriptor Table (IDT) (prose) -> [Ch43-idt-explained.md](docs/chapters/Ch43-idt-explained.md)
- [x] **Ch 40** - Implementing the IDT (config.h, idt/, memory/, kernel hooks) -> [Ch44-implementing-idt.md](docs/chapters/Ch44-implementing-idt.md)
- [x] **Ch 41** - Understanding IN and OUT in Protected Mode (prose) -> [Ch45-in-out-protected-mode.md](docs/chapters/Ch45-in-out-protected-mode.md)
- [x] **Ch 42** - Implementing IN and OUT (src/io/io.asm) -> [Ch46-implementing-in-out.md](docs/chapters/Ch46-implementing-in-out.md)
- [x] **Ch 43** - Programmable Interrupt Controller (PIC) (prose) -> [Ch47-pic.md](docs/chapters/Ch47-pic.md)
- [x] **Ch 44** - Mapping the Programmable Interrupt Controller (prose) -> [Ch48-mapping-pic.md](docs/chapters/Ch48-mapping-pic.md)
- [x] **Ch 45** - Implementing our PIC code (remap, IRQ1 keyboard handler) -> [Ch49-implementing-pic.md](docs/chapters/Ch49-implementing-pic.md)
- [x] **Ch 46** - Memory Allocation in Kernel Development (prose) -> [Ch50-memory-allocation.md](docs/chapters/Ch50-memory-allocation.md)
- [x] **Ch 47** - Our Kernel Heap Design (prose) -> [Ch51-heap-design.md](docs/chapters/Ch51-heap-design.md)
- [x] **Ch 48** - Implementing our Heap (heap.h, heap.c, kheap.h, kheap.c, kmalloc/kfree) -> [Ch52-implementing-heap.md](docs/chapters/Ch52-implementing-heap.md)
- [x] **Ch 49** - Enable/disable interrupts from C -> [Ch53-enable-disable-interrupts.md](docs/chapters/Ch53-enable-disable-interrupts.md)
- [x] **Ch 50** - Understanding paging in 32-bit protected mode (prose) -> [Ch54-paging.md](docs/chapters/Ch54-paging.md)
- [x] **Ch 51** - Implement paging (paging.h, paging.c, paging.asm, identity-mapped 4 GiB, CR0.PG on) -> [Ch55-implementing-paging.md](docs/chapters/Ch55-implementing-paging.md) - test `tests/14-paging-enabled.sh`
- [x] **Ch 52** - Build on paging (paging_is_aligned, paging_get_indexes, paging_set, virt 0x1000 remap demo) -> [Ch56-building-on-paging.md](docs/chapters/Ch56-building-on-paging.md) - test `tests/15-paging-remap.sh`
- [x] **Ch 53** - PCI IDE controller (prose: master/slave, LBA-28, port map at 0x1F0..0x1F7) -> [Ch57-pci-ide-controller.md](docs/chapters/Ch57-pci-ide-controller.md)
- [x] **Ch 54** - Implement disk driver in C (disk.h, disk.c, disk_read_sector via 0x1F0..0x1F7) -> [Ch58-implementing-disk-driver.md](docs/chapters/Ch58-implementing-disk-driver.md) - test `tests/16-disk-reads-sector.sh`
- [x] **Ch 55** - Improve disk driver (struct disk, disk_get, disk_read_block abstraction) -> [Ch59-improving-disk-driver.md](docs/chapters/Ch59-improving-disk-driver.md)
- [x] **Ch 56** - Understanding file systems (prose) -> [Ch60-understanding-filesystems.md](docs/chapters/Ch60-understanding-filesystems.md)
- [x] **Ch 57** - Understanding FAT16 (prose: BPB layout, FAT chain, root dir, data clusters) -> [Ch61-understanding-fat16.md](docs/chapters/Ch61-understanding-fat16.md)
- [x] **Ch 58** - Create src/string/string.{h,c} (strlen, strnlen, isdigit, tonumericdigit) -> [Ch62-string-utils.md](docs/chapters/Ch62-string-utils.md) - test `tests/17-string-utils.sh`
- [x] **Ch 59** - Path parser (SAMOS_MAX_PATH, EBADPATH, memcmp, pparser.{h,c} parses "0:/bin/shell.exe") -> [Ch63-path-parser.md](docs/chapters/Ch63-path-parser.md) - test `tests/18-path-parser.sh`
- [x] **Ch 60** - Streamed disk reading (streamer.h, streamer.c, byte-position seek/read across sectors) -> [Ch64-streamed-disk-reading.md](docs/chapters/Ch64-streamed-disk-reading.md) - test `tests/19-disk-streamer.sh`
- [x] **Ch 61** - FAT16 BPB in boot sector + 16 MiB volume padding (hello.txt parked at repo root) -> [Ch65-fat16-bpb.md](docs/chapters/Ch65-fat16-bpb.md) - test `tests/20-fat16-bpb.sh`
- [x] **Ch 62** - Understanding virtual filesystems (prose) -> [Ch66-understanding-vfs.md](docs/chapters/Ch66-understanding-vfs.md)
- [x] **Ch 63** - VFS scaffolding (file.h/file.c, fs_init, fs_resolve, disk.filesystem; fopen stub) -> [Ch67-vfs-scaffolding.md](docs/chapters/Ch67-vfs-scaffolding.md) - test `tests/21-vfs-fopen-stub.sh`
- [x] **Ch 64** - strcpy (string.h/string.c) -> [Ch68-strcpy.md](docs/chapters/Ch68-strcpy.md) - test `tests/22-strcpy.sh`
- [x] **Ch 65** - FAT16 driver foundations (fat16.h, fat16.c registers with VFS via fs_static_load) -> [Ch69-fat16-stub.md](docs/chapters/Ch69-fat16-stub.md) - test `tests/23-fat16-registers.sh`
- [x] **Ch 66** - FAT16 structures + constants (fat_header, fat_header_extended, fat_directory_item, fat_h, fat_directory, fat_item, fat_item_descriptor, fat_private) -> [Ch70-fat16-structures.md](docs/chapters/Ch70-fat16-structures.md) - test `tests/24-fat16-struct-sizes.sh`
- [x] **Ch 67** - disk.id and disk.fs_private fields (init id=0; fs_private stays null until a driver writes it) -> [Ch71-disk-id-and-private.md](docs/chapters/Ch71-disk-id-and-private.md) - test `tests/25-disk-id-and-fs-private.sh`
- [x] **Ch 68** - FAT16 resolver + root directory walk + EFSNOTUS (reads BPB, validates 0x29 sig, allocates fat_private, walks root dir) -> [Ch72-fat16-resolver.md](docs/chapters/Ch72-fat16-resolver.md) - test `tests/26-fat16-resolver.sh`
- [x] **Ch 69** - String comparison helpers (tolower, strncmp, istrncmp, strnlen_terminator) -> [Ch73-string-cmp.md](docs/chapters/Ch73-string-cmp.md) - test `tests/27-string-cmp.sh`
- [x] **Ch 70** - VFS fopen real impl (ERROR/ERROR_I/ISERR macros, file_get_mode_by_string, path-parse + disk lookup + dispatch via disk-&gt;filesystem-&gt;open) -> [Ch74-vfs-fopen.md](docs/chapters/Ch74-vfs-fopen.md) - test `tests/28-vfs-fopen-dispatch.sh`
- [x] **Ch 71** - memcpy (memory.h/memory.c) -> [Ch75-memcpy.md](docs/chapters/Ch75-memcpy.md) - test `tests/29-memcpy.sh`
- [x] **Ch 72** - Real fat16_open (16 helpers: directory walk, FAT chain traversal, item cloning; ERDONLY status; fopen of non-existent path returns 0) -> [Ch76-fat16-open.md](docs/chapters/Ch76-fat16-open.md) - test `tests/30-fat16-open-miss.sh`
- [x] **Ch 73** - VFS fread (FS_READ_FUNCTION typedef, struct filesystem.read, fread dispatch with EINVARG short-circuits) -> [Ch77-vfs-fread.md](docs/chapters/Ch77-vfs-fread.md) - test `tests/31-vfs-fread.sh`
- [x] **Ch 74** - Refine FAT16 init (book corrects Ch 72 `*` to `+` in starting_pos; we already shipped `+`) -> [Ch78-fat16-read-init-fix.md](docs/chapters/Ch78-fat16-read-init-fix.md) (no-op code)
- [x] **Ch 75** - FAT16 read end-to-end: fat16_read wired into VFS, Makefile mformats + mcopies hello.txt into the volume, kernel reads "Hello world!" off disk -> [Ch79-fat16-read.md](docs/chapters/Ch79-fat16-read.md) - test `tests/32-fat16-read.sh`
- [x] **Ch 76** - VFS fseek (FS_SEEK_FUNCTION typedef, .seek in struct filesystem, fseek dispatch shell) -> [Ch80-vfs-fseek.md](docs/chapters/Ch80-vfs-fseek.md) (no smoke probe; FAT16 seek lands next chapter)
- [x] **Ch 77** - FAT16 fseek (SEEK_SET, SEEK_CUR; SEEK_END returns EUNIMP; fopen+fseek+fread yields "llo world!") -> [Ch81-fat16-fseek.md](docs/chapters/Ch81-fat16-fseek.md) - test `tests/32-fat16-read.sh`
- [x] **Ch 78+79** - VFS fstat + FAT16 fstat (struct file_stat, FILE_STAT_READ_ONLY flag, fstat dispatch; hello.txt reports filesize=13, flags=0) -> [Ch82-vfs-fat16-fstat.md](docs/chapters/Ch82-vfs-fat16-fstat.md) - test `tests/33-fstat.sh`

> Remaining anchors filled in as we read: paging, FAT16, ELF, multitasking, syscalls, keyboard, shell.

## Part 6 - Graphics, Windowing & Drivers

> Filled in once we reach it. Anchors: framebuffer, mouse, window events, PCI, PATA, NVMe, sleep syscall.

---

## Gotchas log

Each entry: short title + chapter where surfaced + link to the gotcha note.

- [G01](docs/gotchas/G01-keyboard-drain.md) - keyboard handler must drain port 0x60 (surfaced in Ch 45)
- [G02](docs/gotchas/G02-press-not-release.md) - filter key-release and 0xE0-prefix scancodes (surfaced after G01)
- [G03](docs/gotchas/G03-stale-objects-on-header-edit.md) - struct layout edits in headers silently desync .o files (surfaced in Ch 73)

---
