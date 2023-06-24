/** @file
  SamOs UEFI bootloader (Lecture 71 port of PeachOS.c).

  This sample application bases on EDK2 HelloWorld template
  to print a banner to the UEFI Console.

  Copyright (c) 2006 - 2018, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/PcdLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiApplicationEntryPoint.h>

//
// String token ID of help message text. EDK2 builds this into the .efi
// PE resource section so the application can use '-?' from the UEFI
// Shell.
//
GLOBAL_REMOVE_IF_UNREFERENCED EFI_STRING_ID  mStringHelpTokenId =
    STRING_TOKEN (STR_HELLO_WORLD_HELP_INFORMATION);

/**
  Entry point. UEFI firmware invokes this directly.

  @param[in] ImageHandle    Firmware-allocated handle for the EFI image.
  @param[in] SystemTable    Pointer to the EFI System Table.

  @retval EFI_SUCCESS       Always.
**/
EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
    Print(L"SamOs UEFI bootloader.");
    while(1) {}
    return EFI_SUCCESS;
}
