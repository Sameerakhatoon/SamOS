/** @file
  SamOs UEFI bootloader.

  Reads KERNEL.BIN from the partition we were loaded from, copies it
  into the reserved kernel window at SAMOS_KERNEL_LOCATION (0x100000),
  builds the boot_info block, exits boot services and jumps to it.

  SamOS divergences from the book's Lectures 73-86:
    - Every region the kernel relies on is reserved with AllocatePages
      using an OS-defined memory type, *before* the memory map is taken:
      the kernel window (image + bss + boot stack) and the boot data
      area (boot_info + memory map).
    - The memory map handed to the kernel is taken immediately before
      ExitBootServices, after all of the loader's own allocations, with
      the real MapKey, retrying on EFI_INVALID_PARAMETER. It exports
      every region with a type, not only EfiConventionalMemory: boot
      services and loader memory come through as "reclaimable".
    - No Print() between the final GetMemoryMap and ExitBootServices
      (Print can allocate, which invalidates the MapKey).
    - The handoff is a single asm statement with register constraints,
      passing one pointer (struct samos_boot_info) in rdi.
    - The GOP pixel format is checked and a usable mode is chosen.

  The same file builds BOOTAA64.EFI (MDE_CPU_AARCH64). There
  KERNEL.BIN is the arm64 Image (bin/aarch64/kernel.img), placed at
  0x40200000 or any 2 MiB boundary; the boot data area is allocated
  anywhere; the DTB (if the firmware gives one) is copied into kernel
  memory; an optional \INITRD.IMG becomes the RAM disk; GOP is
  optional. The kernel is entered as Linux's booting.rst asks, with
  the boot_info pointer in x0: see JumpToKernel.

  Copyright (c) 2006 - 2018, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/PcdLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Guid/FileInfo.h>
#include <Guid/Acpi.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/GraphicsOutput.h>
#include "./SamOs64Bit/src/config.h"
#include "./SamOs64Bit/src/boot/boot_info.h"

// UEFI reserves memory types 0x80000000..0xFFFFFFFF for OS loaders.
// Pages we allocate with this type show up in the final map as
// SAMOS_E820_TYPE_KERNEL, so nothing in the kernel mistakes them for
// free RAM.
#define SAMOS_EFI_KERNEL_MEMORY  ((EFI_MEMORY_TYPE)0x80000000)

// Lecture 75 - E820-format entries the kernel's memory.c reads via
// the SAMOS_MEMORY_MAP_* macros.
typedef struct __attribute__((packed)) E820Entry {
    UINT64 base_addr;
    UINT64 length;
    UINT32 type;
    UINT32 extended_attr;
} E820Entry;

typedef struct __attribute__((packed)) E820Entries {
    UINT64    count;
    E820Entry entries[];
} E820Entries;

EFI_HANDLE        imageHandle  = NULL;
EFI_SYSTEM_TABLE* systemTable  = NULL;

#if defined(MDE_CPU_AARCH64)
// EmbeddedPkg's gFdtTableGuid (DTB_TABLE_GUID), spelled out so the
// module needs no EmbeddedPkg.
STATIC EFI_GUID mFdtTableGuid = { 0xb1b621d5, 0xf19c, 0x41a5, { 0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0 } };

// The arm64 Image header (Linux Documentation/arch/arm64/booting.rst).
typedef struct {
    UINT32 Code0;
    UINT32 Code1;
    UINT64 TextOffset;
    UINT64 ImageSize;       // file plus .bss and stack, from the load address
    UINT64 Flags;
    UINT64 Res2, Res3, Res4;
    UINT32 Magic;           // "ARM\x64"
    UINT32 Res5;
} ARM64_IMAGE_HEADER;

#define ARM64_IMAGE_MAGIC     0x644d5241
#define AARCH64_KERNEL_BASE   0x40200000ULL   // RAM base + 2 MiB on -machine virt
#endif

STATIC UINT32 EfiTypeToE820(UINT32 Type)
{
    switch (Type) {
    case EfiConventionalMemory:
        return SAMOS_E820_TYPE_USABLE;
    case EfiLoaderCode:
    case EfiLoaderData:
    case EfiBootServicesCode:
    case EfiBootServicesData:
        return SAMOS_E820_TYPE_RECLAIMABLE;
    case EfiACPIReclaimMemory:
        return SAMOS_E820_TYPE_ACPI_RECLAIMABLE;
    case EfiACPIMemoryNVS:
        return SAMOS_E820_TYPE_ACPI_NVS;
    case EfiUnusableMemory:
        return SAMOS_E820_TYPE_BAD;
    case EfiPersistentMemory:
        return SAMOS_E820_TYPE_PERSISTENT;
    default:
        if (Type == (UINT32)SAMOS_EFI_KERNEL_MEMORY) {
            return SAMOS_E820_TYPE_KERNEL;
        }
        // Runtime services, MMIO, reserved, PAL code.
        return SAMOS_E820_TYPE_RESERVED;
    }
}

// Convert a UEFI memory map into the E820 table in the boot data
// area, merging neighbours of the same type. Touches no boot
// services, so it is safe between GetMemoryMap and
// ExitBootServices.
STATIC VOID ConvertMemoryMap(EFI_MEMORY_DESCRIPTOR* Map, UINTN MapSize,
                             UINTN DescriptorSize, E820Entries* Out)
{
    UINT64 Count = 0;
    UINT8* Cursor = (UINT8*)Map;
    UINT8* End    = Cursor + MapSize;

    for (; Cursor + DescriptorSize <= End; Cursor += DescriptorSize) {
        EFI_MEMORY_DESCRIPTOR* Desc = (EFI_MEMORY_DESCRIPTOR*)Cursor;
        UINT32 Type   = EfiTypeToE820(Desc->Type);
        UINT64 Base   = Desc->PhysicalStart;
        UINT64 Length = Desc->NumberOfPages * EFI_PAGE_SIZE;

        if (Count > 0) {
            E820Entry* Prev = &Out->entries[Count - 1];
            if (Prev->type == Type && Prev->base_addr + Prev->length == Base) {
                Prev->length += Length;
                continue;
            }
        }
        if (Count >= SAMOS_MEMORY_MAP_MAX_ENTRIES) {
            break;   // table full; the tail is dropped (thousands of entries)
        }
        Out->entries[Count].base_addr     = Base;
        Out->entries[Count].length        = Length;
        Out->entries[Count].type          = Type;
        Out->entries[Count].extended_attr = 0;
        Count++;
    }
    Out->count = Count;
}

//
// Read a file from the FAT partition we were loaded from. The whole
// content gets AllocatePool'd; caller frees with FreePool. With
// Quiet set, a missing file is not reported.
//
EFI_STATUS ReadFileFromCurrentFilesystem(CHAR16* FileName,
                                         VOID** Buffer_Out,
                                         UINTN* BufferSize_Out,
                                         BOOLEAN Quiet)
{
    EFI_STATUS                        Status = 0;
    EFI_LOADED_IMAGE_PROTOCOL*        LoadedImageProtocol = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL*  SimpleFileSystem    = NULL;
    EFI_FILE_PROTOCOL*                Root                = NULL;
    EFI_FILE_PROTOCOL*                File                = NULL;
    UINTN                             FileInfoSize        = 0;

    *Buffer_Out     = NULL;
    *BufferSize_Out = 0;

    Status = gBS->HandleProtocol(imageHandle, &gEfiLoadedImageProtocolGuid,
                                 (VOID**)&LoadedImageProtocol);
    if (EFI_ERROR(Status)) {
        Print(L"Error accessing LoadedImageProtocol: %r\n", Status);
        return Status;
    }

    Status = gBS->HandleProtocol(LoadedImageProtocol->DeviceHandle,
                                 &gEfiSimpleFileSystemProtocolGuid,
                                 (VOID**)&SimpleFileSystem);
    if (EFI_ERROR(Status)) {
        Print(L"Error accessing SimpleFileSystem: %r\n", Status);
        return Status;
    }

    Status = SimpleFileSystem->OpenVolume(SimpleFileSystem, &Root);
    if (EFI_ERROR(Status)) {
        Print(L"Error opening root directory: %r\n", Status);
        return Status;
    }

    Status = Root->Open(Root, &File, FileName, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status)) {
        if (!Quiet) {
            Print(L"Error opening file %s: %r\n", FileName, Status);
        }
        Root->Close(Root);
        return Status;
    }

    FileInfoSize = OFFSET_OF(EFI_FILE_INFO, FileName) + 256 * sizeof(CHAR16);
    EFI_FILE_INFO* FileInfo = AllocatePool(FileInfoSize);
    if (FileInfo == NULL) {
        File->Close(File);
        Root->Close(Root);
        return EFI_OUT_OF_RESOURCES;
    }

    Status = File->GetInfo(File, &gEfiFileInfoGuid, &FileInfoSize, FileInfo);
    if (EFI_ERROR(Status)) {
        Print(L"Error getting file info for %s %r\n", FileName, Status);
        FreePool(FileInfo);
        File->Close(File);
        Root->Close(Root);
        return Status;
    }

    UINTN BufferSize = FileInfo->FileSize;
    FreePool(FileInfo);

    VOID* Buffer = AllocatePool(BufferSize ? BufferSize : 1);
    if (Buffer == NULL) {
        File->Close(File);
        Root->Close(Root);
        return EFI_OUT_OF_RESOURCES;
    }

    Status = File->Read(File, &BufferSize, Buffer);
    File->Close(File);
    Root->Close(Root);
    if (EFI_ERROR(Status)) {
        Print(L"Error reading file %s %r\n", FileName, Status);
        FreePool(Buffer);
        return Status;
    }

    *Buffer_Out     = Buffer;
    *BufferSize_Out = BufferSize;
    return EFI_SUCCESS;
}

// Choose a framebuffer mode the kernel can draw to. The graphics code
// writes 32-bit B,G,R,X pixels. Keep the firmware's current mode if it
// is BGRX; otherwise switch to the BGRX mode closest to 1024x768; fall
// back to an RGBX mode (the kernel swaps red and blue). BitMask and
// BltOnly modes have no linear framebuffer we understand.
//
// Real firmware: the mode it leaves set can be anything (the
// panel's 3840 x 2160, 1366 x 768, a pitch wider than the width), and
// some list modes that are not usable. A usable mode is 32-bit BGRX or
// RGBX with a pitch of at least its width. Among those, a mode no
// larger than GOP_MAX_WIDTH x GOP_MAX_HEIGHT and at least 640 x 480
// "fits": the kernel keeps two copies of the screen in RAM and redraws
// in software, so a 4K screen costs 64 MiB and seconds per redraw.
// Order: fits, then BGRX, then closest to 1024 x 768. The firmware's
// mode is kept when it is BGRX and fits.
#define GOP_MAX_WIDTH   1920
#define GOP_MAX_HEIGHT  1200
#define GOP_SCORE_TIER  1000000

STATIC INT64 GopModeScore(CONST EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* M)
{
    BOOLEAN Bgrx = M->PixelFormat == PixelBlueGreenRedReserved8BitPerColor;
    if ((!Bgrx && M->PixelFormat != PixelRedGreenBlueReserved8BitPerColor) ||
        M->HorizontalResolution == 0 || M->VerticalResolution == 0 ||
        M->PixelsPerScanLine < M->HorizontalResolution) {
        return -1;
    }
    BOOLEAN Fits = M->HorizontalResolution >= 640 && M->VerticalResolution >= 480 &&
                   M->HorizontalResolution <= GOP_MAX_WIDTH && M->VerticalResolution <= GOP_MAX_HEIGHT;
    INT64 Dx = (INT64)M->HorizontalResolution - 1024;
    INT64 Dy = (INT64)M->VerticalResolution - 768;
    INT64 Distance = (Dx < 0 ? -Dx : Dx) + (Dy < 0 ? -Dy : Dy);
    if (Distance >= GOP_SCORE_TIER) {
        Distance = GOP_SCORE_TIER - 1;
    }
    return (Fits ? 4 : 0) * GOP_SCORE_TIER + (Bgrx ? 2 : 1) * GOP_SCORE_TIER - Distance;
}

STATIC EFI_STATUS SetupGraphics(struct samos_boot_info* Info)
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL* Gop = NULL;
    EFI_STATUS Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID**)&Gop);
    if (EFI_ERROR(Status)) {
        Print(L"Error: unable to locate GOP: %r\n", Status);
        return Status;
    }

    // Keep the firmware's mode only when it is BGRX and within
    // the size limit; otherwise the best mode by GopModeScore.
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Current = Gop->Mode->Info;
    BOOLEAN KeepCurrent = Current->PixelFormat == PixelBlueGreenRedReserved8BitPerColor &&
                          GopModeScore(Current) >= 4 * GOP_SCORE_TIER &&
                          Gop->Mode->FrameBufferBase != 0;
    Print(L"GOP: %u modes, current %u (%u x %u, pitch %u, format %u)%s\n",
          Gop->Mode->MaxMode, Gop->Mode->Mode, Current->HorizontalResolution,
          Current->VerticalResolution, Current->PixelsPerScanLine, Current->PixelFormat,
          KeepCurrent ? L", kept" : L"");
    if (!KeepCurrent) {
        INT64  BestScore = -1;
        UINT32 BestMode  = Gop->Mode->Mode;
        for (UINT32 Mode = 0; Mode < Gop->Mode->MaxMode; Mode++) {
            EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* ModeInfo;
            UINTN SizeOfInfo;
            if (EFI_ERROR(Gop->QueryMode(Gop, Mode, &SizeOfInfo, &ModeInfo))) {
                continue;
            }
            INT64 Score = GopModeScore(ModeInfo);
            FreePool(ModeInfo);
            if (Score > BestScore) {
                BestScore = Score;
                BestMode  = Mode;
            }
        }
        if (BestScore < 0) {
            Print(L"Error: no GOP mode with a 32-bit linear framebuffer\n");
            return EFI_UNSUPPORTED;
        }
        if (BestMode != Gop->Mode->Mode) {
            Status = Gop->SetMode(Gop, BestMode);
            if (EFI_ERROR(Status)) {
                // The old mode is still set: use it if we can.
                Print(L"GOP SetMode(%u) failed: %r\n", BestMode, Status);
                if (GopModeScore(Gop->Mode->Info) < 0) {
                    return Status;
                }
            }
        }
    }
    if (Gop->Mode->FrameBufferBase == 0 || GopModeScore(Gop->Mode->Info) < 0) {
        Print(L"Error: GOP mode %u has no linear framebuffer\n", Gop->Mode->Mode);
        return EFI_UNSUPPORTED;
    }

    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* ModeInfo = Gop->Mode->Info;
    Info->fb_base   = Gop->Mode->FrameBufferBase;
    Info->fb_size   = Gop->Mode->FrameBufferSize;
    Info->fb_width  = ModeInfo->HorizontalResolution;
    Info->fb_height = ModeInfo->VerticalResolution;
    Info->fb_pitch  = ModeInfo->PixelsPerScanLine;
    Info->fb_format = ModeInfo->PixelFormat == PixelRedGreenBlueReserved8BitPerColor
                          ? SAMOS_FB_FORMAT_RGBX : SAMOS_FB_FORMAT_BGRX;

    Print(L"Framebuffer %lx (%lu bytes), %u x %u, pitch %u, format %u\n",
          Info->fb_base, Info->fb_size, Info->fb_width, Info->fb_height,
          Info->fb_pitch, Info->fb_format);

    // Paint the screen green as a visible "loader got this far" sign.
    UINT32* Pixels = (UINT32*)(UINTN)Info->fb_base;
    for (UINT32 y = 0; y < Info->fb_height; y++) {
        for (UINT32 x = 0; x < Info->fb_width; x++) {
            Pixels[y * Info->fb_pitch + x] = 0x0000FF00;   // green in both BGRX and RGBX
        }
    }
    return EFI_SUCCESS;
}

STATIC UINT64 FindAcpiRsdp(VOID)
{
    UINT64 Rsdp1 = 0;
    for (UINTN i = 0; i < gST->NumberOfTableEntries; i++) {
        EFI_CONFIGURATION_TABLE* Table = &gST->ConfigurationTable[i];
        if (CompareGuid(&Table->VendorGuid, &gEfiAcpi20TableGuid)) {
            return (UINT64)(UINTN)Table->VendorTable;
        }
        if (CompareGuid(&Table->VendorGuid, &gEfiAcpi10TableGuid)) {
            Rsdp1 = (UINT64)(UINTN)Table->VendorTable;
        }
    }
    return Rsdp1;
}

// Optional \CMDLINE.TXT on the ESP becomes boot_info.cmdline.
STATIC VOID LoadCommandLine(struct samos_boot_info* Info)
{
    VOID*  Buffer = NULL;
    UINTN  Size   = 0;
    if (EFI_ERROR(ReadFileFromCurrentFilesystem(L"CMDLINE.TXT", &Buffer, &Size, TRUE))) {
        return;
    }
    UINTN n = 0;
    CHAR8* Text = Buffer;
    for (UINTN i = 0; i < Size && n < SAMOS_BOOT_CMDLINE_MAX - 1; i++) {
        CHAR8 c = Text[i];
        if (c == '\r' || c == '\n' || c == '\t') {
            c = ' ';
        }
        if (c < ' ' || c > '~') {
            continue;
        }
        Info->cmdline[n++] = c;
    }
    while (n > 0 && Info->cmdline[n - 1] == ' ') {
        n--;
    }
    Info->cmdline[n] = 0;
    FreePool(Buffer);
    Print(L"Command line: %a\n", Info->cmdline);
}

#if !defined(MDE_CPU_AARCH64)
STATIC EFI_STATUS AllocateFixed(EFI_PHYSICAL_ADDRESS Address, UINTN Pages, CONST CHAR16* What)
{
    EFI_PHYSICAL_ADDRESS Base = Address;
    EFI_STATUS Status = gBS->AllocatePages(AllocateAddress, SAMOS_EFI_KERNEL_MEMORY, Pages, &Base);
    if (EFI_ERROR(Status)) {
        Print(L"Error: cannot reserve %s at %lx (%lu pages): %r\n", What, Address, Pages, Status);
    }
    return Status;
}
#else
// Size bytes of kernel memory at Preferred if that is free, else at
// any Align boundary (the oversized allocation's ends are given back).
STATIC EFI_STATUS AllocateKernelPages(UINTN Size, UINTN Align, EFI_PHYSICAL_ADDRESS Preferred,
                                      EFI_PHYSICAL_ADDRESS* Out, CONST CHAR16* What)
{
    UINTN Pages = EFI_SIZE_TO_PAGES(Size);
    EFI_PHYSICAL_ADDRESS Base = Preferred;
    if (Preferred &&
        !EFI_ERROR(gBS->AllocatePages(AllocateAddress, SAMOS_EFI_KERNEL_MEMORY, Pages, &Base))) {
        *Out = Base;
        return EFI_SUCCESS;
    }
    UINTN Slack = Align > EFI_PAGE_SIZE ? EFI_SIZE_TO_PAGES(Align) : 0;
    EFI_STATUS Status = gBS->AllocatePages(AllocateAnyPages, SAMOS_EFI_KERNEL_MEMORY, Pages + Slack, &Base);
    if (EFI_ERROR(Status)) {
        Print(L"Error: cannot allocate %lu bytes for the %s: %r\n", (UINT64)Size, What, Status);
        return Status;
    }
    EFI_PHYSICAL_ADDRESS Aligned = ALIGN_VALUE(Base, (UINT64)(Slack ? Align : EFI_PAGE_SIZE));
    if (Aligned > Base) {
        gBS->FreePages(Base, EFI_SIZE_TO_PAGES(Aligned - Base));
    }
    EFI_PHYSICAL_ADDRESS End = Aligned + EFI_PAGES_TO_SIZE(Pages);
    EFI_PHYSICAL_ADDRESS AllocEnd = Base + EFI_PAGES_TO_SIZE(Pages + Slack);
    if (AllocEnd > End) {
        gBS->FreePages(End, EFI_SIZE_TO_PAGES(AllocEnd - End));
    }
    *Out = Aligned;
    return EFI_SUCCESS;
}

// KERNEL.BIN is an arm64 Image: 2 MiB aligned base + text_offset,
// image_size bytes reserved (the file, then .bss and the boot stack,
// zeroed).
STATIC EFI_STATUS LoadKernelImage(struct samos_boot_info* Info)
{
    VOID*  Buffer = NULL;
    UINTN  Size   = 0;
    EFI_STATUS Status = ReadFileFromCurrentFilesystem(L"kernel.bin", &Buffer, &Size, FALSE);
    if (EFI_ERROR(Status)) {
        Print(L"Error reading kernel: %r\n", Status);
        return Status;
    }
    ARM64_IMAGE_HEADER* Header = Buffer;
    if (Size < sizeof(*Header) || Header->Magic != ARM64_IMAGE_MAGIC) {
        Print(L"Error: kernel.bin is not an arm64 Image\n");
        FreePool(Buffer);
        return EFI_LOAD_ERROR;
    }
    UINT64 Span = Header->TextOffset + MAX(Header->ImageSize, (UINT64)Size);
    EFI_PHYSICAL_ADDRESS Base;
    Status = AllocateKernelPages(Span, SIZE_2MB, AARCH64_KERNEL_BASE, &Base, L"kernel");
    if (EFI_ERROR(Status)) {
        FreePool(Buffer);
        return Status;
    }
    SetMem((VOID*)(UINTN)Base, EFI_PAGES_TO_SIZE(EFI_SIZE_TO_PAGES(Span)), 0);
    CopyMem((VOID*)(UINTN)(Base + Header->TextOffset), Buffer, Size);
    FreePool(Buffer);
    Info->kernel_start = Base + Header->TextOffset;
    Info->kernel_end   = Base + Span;
    Print(L"Kernel: %lu bytes at %lx, image %lx-%lx\n", (UINT64)Size, Info->kernel_start,
          Info->kernel_start, Info->kernel_end);
    return EFI_SUCCESS;
}

// The firmware's DTB, copied into kernel memory: the kernel reads it
// with the MMU off and keeps it, and the original may sit in boot
// services memory the kernel will reuse.
STATIC VOID CopyDeviceTree(struct samos_boot_info* Info)
{
    for (UINTN i = 0; i < gST->NumberOfTableEntries; i++) {
        EFI_CONFIGURATION_TABLE* Table = &gST->ConfigurationTable[i];
        if (!CompareGuid(&Table->VendorGuid, &mFdtTableGuid)) {
            continue;
        }
        UINT8* Fdt = Table->VendorTable;
        UINT32 Magic = SwapBytes32(*(UINT32*)Fdt);
        UINT32 Size  = SwapBytes32(*(UINT32*)(Fdt + 4));
        EFI_PHYSICAL_ADDRESS Copy;
        if (Magic != 0xd00dfeed || EFI_ERROR(AllocateKernelPages(Size, EFI_PAGE_SIZE, 0, &Copy, L"device tree"))) {
            Print(L"Device tree at %p unusable, ignored\n", Fdt);
            return;
        }
        CopyMem((VOID*)(UINTN)Copy, Fdt, Size);
        Info->dtb = Copy;
        Print(L"Device tree: %u bytes, copied to %lx\n", Size, Copy);
        return;
    }
    Print(L"Device tree: none (ACPI only)\n");
}

// Optional \INITRD.IMG on the ESP: the RAM disk (QEMU's -initrd on
// the -kernel path).
STATIC VOID LoadInitrd(struct samos_boot_info* Info)
{
    VOID*  Buffer = NULL;
    UINTN  Size   = 0;
    EFI_PHYSICAL_ADDRESS Base;
    if (EFI_ERROR(ReadFileFromCurrentFilesystem(L"INITRD.IMG", &Buffer, &Size, TRUE))) {
        return;
    }
    if (!EFI_ERROR(AllocateKernelPages(Size, EFI_PAGE_SIZE, 0, &Base, L"initrd"))) {
        CopyMem((VOID*)(UINTN)Base, Buffer, Size);
        Info->initrd_base = Base;
        Info->initrd_size = Size;
        Print(L"Initrd: %lu bytes at %lx\n", (UINT64)Size, Base);
    }
    FreePool(Buffer);
}

// Enter the kernel as booting.rst asks: MMU and D-cache off at the
// current EL (EL1, or EL2 with virtualization=on), the kernel image
// and everything it reads or writes before its own MMU is on cleaned
// to the point of coherency, I-cache invalidated, x0 = boot_info.
// One asm block that touches no memory but the map it reads: any
// store (a stack spill) after the clean would leave a dirty line
// that could be evicted over memory the kernel has since written
// with its caches off. The clean covers every
// RAM range in the E820 table the kernel may use: usable,
// reclaimable (loader and boot services) and kernel.
STATIC VOID __attribute__((noreturn)) JumpToKernel(struct samos_boot_info* Info)
{
    register UINT64 X0 __asm__("x0") = (UINT64)(UINTN)Info;
    register UINT64 X1 __asm__("x1") = Info->kernel_start;
    register UINT64 X2 __asm__("x2") = Info->memory_map;
    __asm__ __volatile__(
        "msr    daifset, #0xf\n\t"
        "mrs    x3, ctr_el0\n\t"
        "ubfx   x3, x3, #16, #4\n\t"
        "mov    x4, #4\n\t"
        "lsl    x4, x4, x3\n\t"             // x4 = D-cache line (DminLine words)
        "sub    x5, x4, #1\n\t"
        "ldr    x3, [x2], #8\n\t"           // x3 = entries, x2 = first entry
        "1:\n\t"
        "cbz    x3, 4f\n\t"
        "ldp    x6, x7, [x2]\n\t"           // base, length
        "ldr    w8, [x2, #16]\n\t"          // type
        "add    x2, x2, #24\n\t"
        "sub    x3, x3, #1\n\t"
        "cmp    w8, #1\n\t"                 // SAMOS_E820_TYPE_USABLE
        "b.eq   2f\n\t"
        "mov    w9, #0x1000\n\t"            // _RECLAIMABLE
        "cmp    w8, w9\n\t"
        "b.eq   2f\n\t"
        "add    w9, w9, #1\n\t"             // _KERNEL
        "cmp    w8, w9\n\t"
        "b.ne   1b\n\t"
        "2:\n\t"
        "add    x7, x6, x7\n\t"
        "bic    x6, x6, x5\n\t"
        "3:\n\t"
        "cmp    x6, x7\n\t"
        "b.hs   1b\n\t"
        "dc     civac, x6\n\t"
        "add    x6, x6, x4\n\t"
        "b      3b\n\t"
        "4:\n\t"
        "dsb    sy\n\t"
        "mrs    x3, CurrentEL\n\t"
        "cmp    x3, #8\n\t"                 // EL2
        "b.ne   5f\n\t"
        "mrs    x3, sctlr_el2\n\t"
        "bic    x3, x3, #1\n\t"             // M
        "bic    x3, x3, #4\n\t"             // C
        "msr    sctlr_el2, x3\n\t"
        "b      6f\n\t"
        "5:\n\t"
        "mrs    x3, sctlr_el1\n\t"
        "bic    x3, x3, #1\n\t"
        "bic    x3, x3, #4\n\t"
        "msr    sctlr_el1, x3\n\t"
        "6:\n\t"
        "isb\n\t"
        "ic     iallu\n\t"
        "dsb    nsh\n\t"
        "isb\n\t"
        "br     x1\n\t"
        :
        : "r"(X0), "r"(X1), "r"(X2)
        : "x3", "x4", "x5", "x6", "x7", "x8", "x9", "memory");
    __builtin_unreachable();
}
#endif

EFI_STATUS
EFIAPI
UefiMain(IN EFI_HANDLE       ImageHandle,
         IN EFI_SYSTEM_TABLE *SystemTable)
{
    imageHandle  = ImageHandle;
    systemTable  = SystemTable;
    EFI_STATUS Status;

    Print(L"SamOs UEFI bootloader.\n");

#if defined(MDE_CPU_AARCH64)
    // 1-2. The boot data area anywhere (boot_info, then the map one
    //      page in, as at 0x20F000/0x210000 on x86), the kernel, the
    //      DTB and the initrd.
    EFI_PHYSICAL_ADDRESS BootData;
    Status = AllocateKernelPages(SAMOS_BOOT_DATA_PAGES * EFI_PAGE_SIZE, EFI_PAGE_SIZE, 0, &BootData,
                                 L"boot data area");
    if (EFI_ERROR(Status)) {
        return Status;
    }
    struct samos_boot_info* Info = (struct samos_boot_info*)(UINTN)BootData;
    E820Entries* MemoryMapOut     = (E820Entries*)(UINTN)(BootData + EFI_PAGE_SIZE);
    SetMem(Info, SAMOS_BOOT_DATA_PAGES * EFI_PAGE_SIZE, 0);
    Info->magic        = SAMOS_BOOT_INFO_MAGIC;
    Info->version      = SAMOS_BOOT_INFO_VERSION;
    Info->size         = sizeof(struct samos_boot_info);
    Info->memory_map   = (UINT64)(UINTN)MemoryMapOut;
    Info->acpi_rsdp    = FindAcpiRsdp();
    Status = LoadKernelImage(Info);
    if (EFI_ERROR(Status)) {
        return Status;
    }
    CopyDeviceTree(Info);
    Print(L"ACPI RSDP: %lx\n", Info->acpi_rsdp);
    LoadInitrd(Info);
#else
    // 1. Reserve the fixed regions the kernel uses, before anything
    //    else can land there.
    Status = AllocateFixed(SAMOS_KERNEL_LOCATION,
                           EFI_SIZE_TO_PAGES(SAMOS_KERNEL_WINDOW_END - SAMOS_KERNEL_LOCATION),
                           L"kernel window");
    if (EFI_ERROR(Status)) {
        return Status;
    }
    Status = AllocateFixed(SAMOS_BOOT_DATA_LOCATION, SAMOS_BOOT_DATA_PAGES, L"boot data area");
    if (EFI_ERROR(Status)) {
        return Status;
    }

    struct samos_boot_info* Info = (struct samos_boot_info*)(UINTN)SAMOS_BOOT_INFO_LOCATION;
    E820Entries* MemoryMapOut     = (E820Entries*)(UINTN)SAMOS_MEMORY_MAP_TOTAL_ENTRIES_LOCATION;
    SetMem((VOID*)(UINTN)SAMOS_BOOT_DATA_LOCATION, SAMOS_BOOT_DATA_PAGES * EFI_PAGE_SIZE, 0);
    Info->magic        = SAMOS_BOOT_INFO_MAGIC;
    Info->version      = SAMOS_BOOT_INFO_VERSION;
    Info->size         = sizeof(struct samos_boot_info);
    Info->memory_map   = SAMOS_MEMORY_MAP_TOTAL_ENTRIES_LOCATION;
    Info->kernel_start = SAMOS_KERNEL_LOCATION;
    Info->kernel_end   = SAMOS_KERNEL_WINDOW_END;
    Info->acpi_rsdp    = FindAcpiRsdp();

    // 2. Load the kernel image into the window. Everything past the
    //    file (the kernel's .bss and boot stack) is zeroed.
    VOID*  KernelBuffer     = NULL;
    UINTN  KernelBufferSize = 0;
    Status = ReadFileFromCurrentFilesystem(L"kernel.bin", &KernelBuffer, &KernelBufferSize, FALSE);
    if (EFI_ERROR(Status)) {
        Print(L"Error reading kernel: %r\n", Status);
        return Status;
    }
    if (KernelBufferSize > SAMOS_KERNEL_MAX_IMAGE_SIZE) {
        Print(L"Error: kernel.bin is %lu bytes, the window holds %lu\n",
              KernelBufferSize, (UINTN)SAMOS_KERNEL_MAX_IMAGE_SIZE);
        return EFI_BAD_BUFFER_SIZE;
    }
    SetMem((VOID*)(UINTN)SAMOS_KERNEL_LOCATION, SAMOS_KERNEL_WINDOW_END - SAMOS_KERNEL_LOCATION, 0);
    CopyMem((VOID*)(UINTN)SAMOS_KERNEL_LOCATION, KernelBuffer, KernelBufferSize);
    FreePool(KernelBuffer);
    Print(L"Kernel: %lu bytes at %lx\n", KernelBufferSize, (UINT64)SAMOS_KERNEL_LOCATION);
#endif

    LoadCommandLine(Info);

    // 3. Framebuffer.
    Status = SetupGraphics(Info);
    if (EFI_ERROR(Status)) {
#if defined(MDE_CPU_AARCH64)
        // -nographic or no display device: the kernel runs on the UART.
        Info->fb_format = SAMOS_FB_FORMAT_NONE;
#else
        // A PC whose GOP has no mode we can draw to (BltOnly,
        // bitmask, no GOP at all on a headless board) still boots:
        // the kernel draws into RAM it never shows (the refused
        // format), and the serial port and BOOTLOG.TXT carry the boot.
        Print(L"No usable framebuffer: booting without screen output\n");
        Info->fb_base   = 0;
        Info->fb_size   = 0;
        Info->fb_width  = 640;
        Info->fb_height = 480;
        Info->fb_pitch  = 640;
        Info->fb_format = SAMOS_FB_FORMAT_NONE;
#endif
    }

    // 4. Final memory map + ExitBootServices. Size the buffer once,
    //    with headroom because allocating it can split a descriptor:
    //    after a failed ExitBootServices only GetMemoryMap may be
    //    called again, so the loop below must not allocate.
    UINTN  MapSize = 0, MapKey = 0, DescriptorSize = 0, MapCapacity = 0;
    UINT32 DescriptorVersion = 0;
    EFI_MEMORY_DESCRIPTOR* Map = NULL;
    Status = gBS->GetMemoryMap(&MapSize, NULL, &MapKey, &DescriptorSize, &DescriptorVersion);
    if (Status != EFI_BUFFER_TOO_SMALL) {
        Print(L"Error sizing the memory map: %r\n", Status);
        return EFI_ERROR(Status) ? Status : EFI_DEVICE_ERROR;
    }
    MapCapacity = MapSize + 64 * DescriptorSize;
    Map = AllocatePool(MapCapacity);
    if (Map == NULL) {
        Print(L"Error allocating the memory map buffer\n");
        return EFI_OUT_OF_RESOURCES;
    }
    for (UINTN Attempt = 0; Attempt < 8; Attempt++) {
        MapSize = MapCapacity;
        Status = gBS->GetMemoryMap(&MapSize, Map, &MapKey, &DescriptorSize, &DescriptorVersion);
        if (EFI_ERROR(Status)) {
            return Status;
        }

        ConvertMemoryMap(Map, MapSize, DescriptorSize, MemoryMapOut);

        Status = gBS->ExitBootServices(ImageHandle, MapKey);
        if (Status == EFI_SUCCESS) {
            break;
        }
        if (Status != EFI_INVALID_PARAMETER) {
            return Status;
        }
        // EFI_INVALID_PARAMETER: the map changed under us (a timer
        // event allocated, say). Take it again and retry.
    }
    if (EFI_ERROR(Status)) {
        return Status;
    }

    // 5. Hand off. Boot services are gone: no Print, no gBS. One asm
    //    statement, so the compiler can't reuse rdi between setting it
    //    and the jump.
#if defined(MDE_CPU_AARCH64)
    JumpToKernel(Info);
#else
    __asm__ __volatile__(
        "cli\n\t"
        "jmp *%1\n\t"
        :
        : "D"((UINT64)(UINTN)Info), "r"((UINT64)SAMOS_KERNEL_LOCATION)
        : "memory");
    __builtin_unreachable();
#endif
}
