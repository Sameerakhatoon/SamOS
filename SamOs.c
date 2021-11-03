/** @file
  SamOs UEFI bootloader (Lecture 73 - loads kernel.bin from the FAT
  partition, copies it to SAMOS_KERNEL_LOCATION = 0x100000, exits boot
  services, and jumps to it).

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
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include "./SamOs64Bit/src/config.h"

// Lecture 75 - E820-format entries the kernel's existing
// memory.c reads via SAMOS_MEMORY_MAP_* macros.
typedef struct __attribute__((packed)) E820Entry {
    UINT64 base_addr;
    UINT64 length;
    UINT32 type;          // 1 = usable
    UINT32 extended_attr;
} E820Entry;

// Lecture 75 - on-disk container: u64 count + flexible array
// of E820Entry. Written at SAMOS_MEMORY_MAP_TOTAL_ENTRIES_LOCATION
// (count) + SAMOS_MEMORY_MAP_LOCATION (entries).
typedef struct __attribute__((packed)) E820Entries {
    UINT64    count;
    E820Entry entries[];
} E820Entries;

EFI_HANDLE        imageHandle  = NULL;
EFI_SYSTEM_TABLE* systemTable  = NULL;

// Lecture 75 - convert the UEFI memory map to E820 format at
// the addresses the kernel expects.
//
// UEFI replaces BIOS int 0x15/0xE820 with gBS->GetMemoryMap.
// We query the map size first (with a NULL buffer to provoke
// EFI_BUFFER_TOO_SMALL), allocate, query again. Then walk the
// descriptors, filter for EfiConventionalMemory (= usable
// RAM), and write each one as an E820Entry with type=1 at
// SAMOS_MEMORY_MAP_LOCATION. The count goes at
// SAMOS_MEMORY_MAP_TOTAL_ENTRIES_LOCATION as a UINT64.
//
// The kernel's e820_total_entries reads the count as a
// uint16_t, which is fine as long as the entry count fits in
// 16 bits (4096 max - we have maybe 20 usable regions on a
// typical PC).
EFI_STATUS SetupMemoryMaps(void){
    EFI_STATUS             status;
    UINTN                  memoryMapSize     = 0;
    EFI_MEMORY_DESCRIPTOR* memoryMap         = NULL;
    UINTN                  mapKey;
    UINTN                  descriptorSize;
    UINT32                 descriptorVersion;

    // Pass 1: ask UEFI for the buffer size. The expected result
    // is EFI_BUFFER_TOO_SMALL with memoryMapSize updated.
    status = gBS->GetMemoryMap(&memoryMapSize, NULL, &mapKey,
                               &descriptorSize, &descriptorVersion);
    if(status != EFI_BUFFER_TOO_SMALL && EFI_ERROR(status)){
        Print(L"Error retrieving initial memory map size: %r\n", status);
        return status;
    }

    // Pad by 10 descriptors of headroom. AllocatePool itself
    // can change the memory map by adding new descriptors, so
    // pass 2 might come back with more entries than pass 1
    // sized for.
    memoryMapSize += descriptorSize * 10;
    memoryMap = AllocatePool(memoryMapSize);
    if(memoryMap == NULL){
        Print(L"Error allocating memory for memory map\n");
        return EFI_OUT_OF_RESOURCES;
    }

    // Pass 2: real query.
    status = gBS->GetMemoryMap(&memoryMapSize, memoryMap, &mapKey,
                               &descriptorSize, &descriptorVersion);
    if(EFI_ERROR(status)){
        Print(L"Error getting memory map: %r\n", status);
        FreePool(memoryMap);
        return status;
    }

    // Count EfiConventionalMemory descriptors (= usable RAM
    // the bootloader hasn't claimed yet).
    UINTN descriptorCount = memoryMapSize / descriptorSize;
    EFI_MEMORY_DESCRIPTOR* desc = memoryMap;
    UINTN totalConventionalDescriptors = 0;
    for(UINTN i = 0; i < descriptorCount; ++i){
        if(desc->Type == EfiConventionalMemory){
            totalConventionalDescriptors++;
        }
        desc = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)desc + descriptorSize);
    }

    // Reserve fixed pages for the E820 dump.
    EFI_PHYSICAL_ADDRESS MemoryMapLocationE820 = SAMOS_MEMORY_MAP_TOTAL_ENTRIES_LOCATION;
    UINTN MemoryMapSizeE820 = (sizeof(E820Entry) * totalConventionalDescriptors)
                              + sizeof(UINT64);
    status = gBS->AllocatePages(AllocateAddress, EfiLoaderData,
                                EFI_SIZE_TO_PAGES(MemoryMapSizeE820),
                                &MemoryMapLocationE820);
    if(EFI_ERROR(status)){
        Print(L"Error allocating memory for the E820 Entries: %r\n", status);
        return status;
    }

    // Walk again, this time copying out.
    E820Entries* e820Entries = (E820Entries*)MemoryMapLocationE820;
    UINTN ConventionalMemoryIndex = 0;
    desc = memoryMap;
    for(UINTN i = 0; i < descriptorCount; ++i){
        if(desc->Type == EfiConventionalMemory){
            E820Entry* e = &e820Entries->entries[ConventionalMemoryIndex];
            e->base_addr     = desc->PhysicalStart;
            e->length        = desc->NumberOfPages * 4096;
            e->type          = 1;
            e->extended_attr = 0;
            Print(L"e820Entry=%p\n", e);
            ConventionalMemoryIndex++;
        }
        desc = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)desc + descriptorSize);
    }

    e820Entries->count = totalConventionalDescriptors;
    FreePool(memoryMap);
    return EFI_SUCCESS;
}

//
// Read a file from the FAT partition we were loaded from.
//
// EDK2 supplies LoadedImageProtocol (= "the image that launched us")
// which has a DeviceHandle field pointing at the partition. Through
// that handle we can open a SimpleFileSystemProtocol -> the FAT
// driver -> the root directory -> a file. The whole content gets
// AllocatePool'd; caller frees with FreePool when done.
//
EFI_STATUS ReadFileFromCurrentFilesystem(CHAR16* FileName,
                                         VOID** Buffer_Out,
                                         UINTN* BufferSize_Out)
{
    EFI_STATUS                        Status = 0;
    EFI_LOADED_IMAGE_PROTOCOL*        LoadedImageProtocol = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL*  SimpleFileSystem    = NULL;
    EFI_FILE_PROTOCOL*                Root                = NULL;
    EFI_FILE_PROTOCOL*                File                = NULL;
    UINTN                             FileInfoSize        = 0;

    *Buffer_Out     = NULL;
    *BufferSize_Out = 0;

    // Find the device we booted from.
    Status = gBS->HandleProtocol(imageHandle,
                                 &gEfiLoadedImageProtocolGuid,
                                 (VOID**)&LoadedImageProtocol);
    if (EFI_ERROR(Status)) {
        Print(L"Error accessing LoadedImageProtocol: %r\n", Status);
        return Status;
    }

    // Get its FAT driver.
    Status = gBS->HandleProtocol(LoadedImageProtocol->DeviceHandle,
                                 &gEfiSimpleFileSystemProtocolGuid,
                                 (VOID**)&SimpleFileSystem);
    if (EFI_ERROR(Status)) {
        Print(L"Error accessing SimpleFileSystem: %r\n", Status);
        return Status;
    }

    // Open the root.
    Status = SimpleFileSystem->OpenVolume(SimpleFileSystem, &Root);
    if (EFI_ERROR(Status)) {
        Print(L"Error opening root directory: %r\n", Status);
        return Status;
    }

    // Open the file.
    Status = Root->Open(Root, &File, FileName, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status)) {
        Print(L"Error opening file %s: %r\n", FileName, Status);
        return Status;
    }

    // GetInfo wants enough buffer for the FileInfo struct + the
    // longest filename it might contain.
    FileInfoSize = OFFSET_OF(EFI_FILE_INFO, FileName) + 256 * sizeof(CHAR16);
    VOID* FileInfoBuffer = AllocatePool(FileInfoSize);
    if (FileInfoBuffer == NULL) {
        Print(L"Error allocating buffer for file info\n");
        File->Close(File);
        return EFI_OUT_OF_RESOURCES;
    }

    EFI_FILE_INFO* FileInfo = (EFI_FILE_INFO*)FileInfoBuffer;
    Status = File->GetInfo(File, &gEfiFileInfoGuid, &FileInfoSize, FileInfo);
    if (EFI_ERROR(Status)) {
        Print(L"Error getting file info for %s %r\n", FileName, Status);
        FreePool(FileInfoBuffer);
        File->Close(File);
        return Status;
    }

    UINTN BufferSize = FileInfo->FileSize;
    FreePool(FileInfoBuffer);
    FileInfoBuffer = NULL;

    VOID* Buffer = AllocatePool(BufferSize);
    if (Buffer == NULL) {
        Print(L"Error allocating buffer for file %s\n", FileName);
        File->Close(File);
        return EFI_OUT_OF_RESOURCES;
    }

    Status = File->Read(File, &BufferSize, Buffer);
    if (EFI_ERROR(Status)) {
        Print(L"Error reading file %s %r", FileName, Status);
        FreePool(Buffer);
        File->Close(File);
        return Status;
    }

    *Buffer_Out     = Buffer;
    *BufferSize_Out = BufferSize;
    Print(L"Read %d bytes from file %s\n", BufferSize, FileName);

    File->Close(File);
    return EFI_SUCCESS;
}

/*
  UEFI entry point.

  Reads kernel.bin from the partition we booted from, copies it to
  the fixed physical address SAMOS_KERNEL_LOCATION = 0x100000,
  exits UEFI boot services, and jumps to it.

  After ExitBootServices the gBS pointer is invalid; we can only use
  RuntimeServices. The kernel's job is to set up its own GDT/IDT/
  paging from scratch (kernel.asm already does this).
*/
EFI_STATUS
EFIAPI
UefiMain(IN EFI_HANDLE       ImageHandle,
         IN EFI_SYSTEM_TABLE *SystemTable)
{
    imageHandle  = ImageHandle;
    systemTable  = SystemTable;
    EFI_STATUS Status = 0;

    Print(L"SamOs UEFI bootloader.\n");

    // Lecture 76 - dump the UEFI memory map FIRST, before we
    // AllocatePages for the kernel. Otherwise the kernel
    // allocation can grab pages that SetupMemoryMaps wanted to
    // reserve (it AllocatePages's at SAMOS_MEMORY_MAP_TOTAL_
    // ENTRIES_LOCATION = 0x210000), causing one of them to
    // fail. Running this first ensures the dump area is
    // reserved before any other AllocatePages call.
    SetupMemoryMaps();

    VOID*  KernelBuffer     = NULL;
    UINTN  KernelBufferSize = 0;
    Status = ReadFileFromCurrentFilesystem(L"kernel.bin",
                                           &KernelBuffer,
                                           &KernelBufferSize);
    if (EFI_ERROR(Status)) {
        Print(L"Error reading kernel: %r\n", Status);
        return Status;
    }
    Print(L"Kernel file loaded at: %p\n", KernelBuffer);

    // Map kernel at the well-known physical address.
    EFI_PHYSICAL_ADDRESS KernelBase = SAMOS_KERNEL_LOCATION;
    Status = gBS->AllocatePages(AllocateAddress, EfiLoaderData,
                                EFI_SIZE_TO_PAGES(KernelBufferSize),
                                &KernelBase);
    if (EFI_ERROR(Status)) {
        Print(L"Error allocating memory for kernel %r\n", Status);
        return Status;
    }

    CopyMem((VOID*)KernelBase, KernelBuffer, KernelBufferSize);
    Print(L"Kernel copied to: %p\n", KernelBase);

    // After this gBS is invalid.
    gBS->ExitBootServices(ImageHandle, 0);

    // Hand off to the kernel. AT&T jmp *%0 expands to an indirect
    // jump through whichever register the compiler picked.
    __asm__("jmp *%0" : : "r"(KernelBase));

    return EFI_SUCCESS;
}
