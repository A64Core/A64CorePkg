/** @file
  A64Core UEFI application.
**/

#include <Uefi.h>

#include <Library/AcMainLib.h>
#include <Library/UefiLib.h>

EFI_STATUS
EFIAPI
UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  AC_GLOBAL_CONFIG Config = {0};
  EFI_STATUS       Status;
  CONST CHAR16    *Stage;

  Stage = L"Read configuration";
  Status = AcLoadConfiguration(ImageHandle, &Config);
  if (EFI_ERROR(Status))
    goto Done;
  Stage = L"Load early drivers";
  Status = AcLoadDrivers(ImageHandle, &Config, TRUE);
  if (EFI_ERROR(Status))
    goto Done;
  Stage = L"Load ordinary drivers";
  Status = AcLoadDrivers(ImageHandle, &Config, FALSE);
  if (EFI_ERROR(Status))
    goto Done;
  Stage = L"Connect devices";
  Status = AcConnectDrivers();
  if (EFI_ERROR(Status))
    goto Done;
  Stage = L"Run boot picker";
  Status = AcMiscBoot(ImageHandle, &Config);

Done:
  AcConfigurationFree(&Config);
  if (EFI_ERROR(Status) && (SystemTable->ConOut != NULL))
    Print(L"A64Core: %s failed: %r\r\n", Stage, Status);
  return Status;
}
