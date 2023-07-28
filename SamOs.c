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
// back to an RGBX mode (colours swapped, still usable). BitMask and
// BltOnly modes have no linear framebuffer we understand.
STATIC EFI_STATUS SetupGraphics(struct samos_boot_info* Info)
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL* Gop = NULL;
    EFI_STATUS Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID**)&Gop);
    if (EFI_ERROR(Status)) {
        Print(L"Error: unable to locate GOP: %r\n", Status);
        return Status;
    }

    EFI_GRAPHICS_PIXEL_FORMAT CurrentFormat = Gop->Mode->Info->PixelFormat;
    if (CurrentFormat != PixelBlueGreenRedReserved8BitPerColor) {
        INT64  BestScore = -1;
        UINT32 BestMode  = Gop->Mode->Mode;
        for (UINT32 Mode = 0; Mode < Gop->Mode->MaxMode; Mode++) {
            EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* ModeInfo;
            UINTN SizeOfInfo;
            if (EFI_ERROR(Gop->QueryMode(Gop, Mode, &SizeOfInfo, &ModeInfo))) {
                continue;
            }
            INT64 Score = -1;
            INT64 Dx = (INT64)ModeInfo->HorizontalResolution - 1024;
            INT64 Dy = (INT64)ModeInfo->VerticalResolution - 768;
            INT64 Distance = (Dx < 0 ? -Dx : Dx) + (Dy < 0 ? -Dy : Dy);
            if (ModeInfo->PixelFormat == PixelBlueGreenRedReserved8BitPerColor) {
                Score = 2000000 - Distance;
            } else if (ModeInfo->PixelFormat == PixelRedGreenBlueReserved8BitPerColor) {
                Score = 1000000 - Distance;
            }
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
                Print(L"Error: GOP SetMode(%u) failed: %r\n", BestMode, Status);
                return Status;
            }
        }
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

STATIC EFI_STATUS AllocateFixed(EFI_PHYSICAL_ADDRESS Address, UINTN Pages, CONST CHAR16* What)
{
    EFI_PHYSICAL_ADDRESS Base = Address;
    EFI_STATUS Status = gBS->AllocatePages(AllocateAddress, SAMOS_EFI_KERNEL_MEMORY, Pages, &Base);
    if (EFI_ERROR(Status)) {
        Print(L"Error: cannot reserve %s at %lx (%lu pages): %r\n", What, Address, Pages, Status);
    }
    return Status;
}

EFI_STATUS
EFIAPI
UefiMain(IN EFI_HANDLE       ImageHandle,
         IN EFI_SYSTEM_TABLE *SystemTable)
{
    imageHandle  = ImageHandle;
    systemTable  = SystemTable;
    EFI_STATUS Status;

    Print(L"SamOs UEFI bootloader.\n");

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

    LoadCommandLine(Info);

    // 3. Framebuffer.
    Status = SetupGraphics(Info);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    // 4. Final memory map + ExitBootServices. The map buffer gets
    //    headroom because allocating it can split a descriptor.
    UINTN  MapSize = 0, MapKey = 0, DescriptorSize = 0, MapCapacity = 0;
    UINT32 DescriptorVersion = 0;
    EFI_MEMORY_DESCRIPTOR* Map = NULL;
    for (UINTN Attempt = 0; Attempt < 8; Attempt++) {
        MapSize = MapCapacity;
        Status = gBS->GetMemoryMap(&MapSize, Map, &MapKey, &DescriptorSize, &DescriptorVersion);
        if (Status == EFI_BUFFER_TOO_SMALL) {
            if (Map != NULL) {
                FreePool(Map);
            }
            MapCapacity = MapSize + 16 * DescriptorSize;
            Map = AllocatePool(MapCapacity);
            if (Map == NULL) {
                return EFI_OUT_OF_RESOURCES;   // can't Print safely once EBS was tried
            }
            continue;
        }
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
    __asm__ __volatile__(
        "cli\n\t"
        "jmp *%1\n\t"
        :
        : "D"((UINT64)(UINTN)Info), "r"((UINT64)SAMOS_KERNEL_LOCATION)
        : "memory");
    __builtin_unreachable();
}
