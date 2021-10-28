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

EFI_HANDLE        imageHandle  = NULL;
EFI_SYSTEM_TABLE* systemTable  = NULL;

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
