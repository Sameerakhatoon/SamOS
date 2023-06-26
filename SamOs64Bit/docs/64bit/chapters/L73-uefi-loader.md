# Lecture 73 - UEFI bootloader loads kernel.bin

**Source commit (PeachOS64BitCourse):** `4cdd5ee`
**SamOs commit:** L73 on `module1-64bit` branch
**Regression test:** `tests64/L73-uefi-loader-stub.sh`

## Why this chapter exists

L71's `SamOs.c` was print-and-spin. L73 turns it into a real
UEFI bootloader.

## What the bootloader does

```c
UefiMain:
    Print("SamOs UEFI bootloader.")
    ReadFileFromCurrentFilesystem(L"kernel.bin", &buf, &size)
    AllocatePages(AllocateAddress, EfiLoaderData,
                  SIZE_TO_PAGES(size), &KernelBase=0x100000)
    CopyMem(KernelBase, buf, size)
    ExitBootServices(ImageHandle, MapKey)
    asm("jmp *%0" :: "r"(KernelBase))
```

Six steps:
1. Print so the user sees we got control.
2. Use LoadedImageProtocol -> DeviceHandle -> SimpleFileSystem
   to open the FAT partition we booted from, then read
   `kernel.bin` into a UEFI pool buffer.
3. Ask UEFI to allocate physical pages at 0x100000
   (`AllocateAddress` mode means "give me THIS exact address,
   error if it's already in use").
4. Copy the kernel content into those pages.
5. Tell UEFI we're done with boot services. After this only
   RuntimeServices remain; `gBS` is invalid.
6. Jump to the kernel. AT&T `jmp *%0` does an indirect jump
   through a register the compiler picked.

## The LoadedImageProtocol chain

```
imageHandle
  |
  | HandleProtocol(gEfiLoadedImageProtocolGuid)
  v
LoadedImageProtocol -- DeviceHandle (the partition we booted from)
                       |
                       | HandleProtocol(gEfiSimpleFileSystemProtocolGuid)
                       v
                       SimpleFileSystem
                          |
                          | OpenVolume
                          v
                          Root EFI_FILE_PROTOCOL
                             |
                             | Open("kernel.bin", READ)
                             v
                             File EFI_FILE_PROTOCOL
                                |
                                | GetInfo / Read
                                v
                                bytes
```

UEFI hands us a handle to each protocol; we call them on the
handle. The "current filesystem" concept is just "whatever
device my LoadedImageProtocol points at".

## ExitBootServices

After this:
- gBS is invalid
- All UEFI services that aren't in gRT are gone
- The memory map is frozen

The kernel inherits whatever address space UEFI had at the
moment of exit. In our case that includes the kernel at
0x100000 (we just installed it there) plus whatever pages
the bootloader used for its own stack / image.

## SAMOS_KERNEL_LOCATION

Both the BIOS path (where boot.asm does `mov edi, 0x0100000;
call ata_lba_read; jmp CODE_SEG:0x0100000`) and the UEFI
path agree on 0x100000. We hoist that magic number into
`config.h` as `SAMOS_KERNEL_LOCATION` so future changes only
touch one place.

## What's NOT done in CI

The .efi build requires EDK2. SamOs CI verifies:
- The C source contains the key function names + magic
  constants (sanity check via grep)
- The BIOS image still boots to `user enter`

The L73 test does NOT actually run the UEFI path. That needs
OVMF + a manual GPT image build, which is sudo-dependent.

## SamOs vs upstream

- File rename `PeachOS.c -> SamOs.c`, banner text adjusted
- `#define PEACHOS_KERNEL_LOCATION -> SAMOS_KERNEL_LOCATION`
- Otherwise the EDK2 logic is verbatim

## Forward look

L74 - kernel.asm modifications to survive UEFI hand-off
(probably reset segment registers, fix the stack pointer,
re-derive paging - UEFI leaves the CPU in long mode with a
UEFI-flavoured GDT and identity-mapped paging).

L75 - re-detect free memory regions. The E820 BIOS call
doesn't exist under UEFI; the UEFI memory map is a different
shape, retrieved via gBS->GetMemoryMap before ExitBootServices.

L76 - solve the "unmapped kernel" problem. The kernel's
paging code (L13+) builds a new PML4 from scratch. If that
new PML4 doesn't map the kernel's own RIP, the next
instruction faults.
