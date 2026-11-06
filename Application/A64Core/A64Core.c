/** @file
  A64Core UEFI application.
**/

#include <Uefi.h>

#include <Library/AcMainLib.h>

EFI_STATUS
EFIAPI
UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  return AcMiscBoot(ImageHandle);
}
