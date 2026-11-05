/** @file
  Bootstrap UEFI application.
**/

#include <Uefi.h>

#include <Protocol/LoadedImage.h>

#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#define A64CORE_PATH L"\\EFI\\AC\\A64Core.efi"

STATIC VOID
WaitForReturnKey(IN EFI_SIMPLE_TEXT_INPUT_PROTOCOL *Input)
{
  EFI_INPUT_KEY Key;
  EFI_STATUS    Status;
  UINTN         EventIndex;

  if ((Input == NULL) || (Input->WaitForKey == NULL))
  {
    return;
  }

  Status = Input->Reset(Input, FALSE);
  if (EFI_ERROR(Status))
  {
    return;
  }

  Print(L"Press any key to return to firmware.\r\n");
  do
  {
    Status = gBS->WaitForEvent(1, &Input->WaitForKey, &EventIndex);
    if (EFI_ERROR(Status))
    {
      return;
    }

    Status = Input->ReadKeyStroke(Input, &Key);
  } while (Status == EFI_NOT_READY);
}

STATIC VOID
UnloadRemainingImage(IN EFI_HANDLE ImageHandle)
{
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
  EFI_STATUS                 Status;

  if (ImageHandle == NULL)
  {
    return;
  }

  // A returning application is already unloaded; a rejected start may leave it loaded.
  Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage);
  if (!EFI_ERROR(Status))
  {
    (VOID) gBS->UnloadImage(ImageHandle);
  }
}

EFI_STATUS
EFIAPI
UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
  EFI_DEVICE_PATH_PROTOCOL  *A64CorePath;
  EFI_HANDLE                 A64CoreHandle;
  EFI_STATUS                 Status;
  CONST CHAR16              *Stage;
  UINTN                      ExitDataSize;

  A64CoreHandle = NULL;
  Stage = L"Read Bootstrap image information";
  Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage);
  if (EFI_ERROR(Status))
  {
    goto Done;
  }

  Stage = L"Locate source device path";
  if ((LoadedImage->DeviceHandle == NULL) ||
      (DevicePathFromHandle(LoadedImage->DeviceHandle) == NULL))
  {
    Status = EFI_NOT_FOUND;
    goto Done;
  }

  Stage = L"Create A64Core device path";
  A64CorePath = FileDevicePath(LoadedImage->DeviceHandle, A64CORE_PATH);
  if (A64CorePath == NULL)
  {
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }

  Stage = L"Load A64Core";
  Status = gBS->LoadImage(FALSE, ImageHandle, A64CorePath, NULL, 0, &A64CoreHandle);
  FreePool(A64CorePath);
  if (EFI_ERROR(Status))
  {
    goto Done;
  }

  Stage = L"StartImage returned";
  ExitDataSize = 0;
  Status = gBS->StartImage(A64CoreHandle, &ExitDataSize, NULL);

Done:
  UnloadRemainingImage(A64CoreHandle);
  if (SystemTable->ConOut != NULL)
  {
    Print(L"A64Core Bootstrap\r\n%s: %r\r\nTarget: %s\r\n", Stage, Status, A64CORE_PATH);
    WaitForReturnKey(SystemTable->ConIn);
  }

  return Status;
}
