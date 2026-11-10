/** @file
  Primary initialization helpers.
**/

#ifndef AC_MAIN_INTERNAL_H
#define AC_MAIN_INTERNAL_H

#include <Library/AcMainLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Protocol/LoadedImage.h>

EFI_STATUS InternalMainSourceDevice(IN EFI_HANDLE ImageHandle, OUT EFI_HANDLE *Device);

VOID InternalMainReport(IN CONST CHAR16 *Stage, IN CONST CHAR16 *Path OPTIONAL,
                        IN EFI_STATUS Status);

#endif
