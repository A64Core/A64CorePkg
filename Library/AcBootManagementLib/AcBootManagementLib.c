/** @file
  A64Core boot picker execution loop.
**/

#include "BootManagementInternal.h"

#include <Library/UefiRuntimeServicesTableLib.h>

EFI_STATUS
AcRunBootPicker(IN EFI_HANDLE ImageHandle, IN CONST AC_GLOBAL_CONFIG *Config)
{
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *Input;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *Output;
  EFI_STATUS                       Status;
  EFI_STATUS                       ResolveStatus;
  EFI_STATUS                       KernelStatus;
  BOOLEAN                          ResetReturned;
  BOOLEAN                          Rescan;
  AC_BOOT_ENTRY                   *Entries = NULL;
  UINTN                            EntryCount = 0;
  UINTN                            Selected;
  AC_BOOT_MENU_ACTION              Action;
  AC_BOOT_RESOURCES                Resources = {0};
  AC_BOOT_KERNEL                   Kernel = {0};

  if (ImageHandle == NULL || Config == NULL)
    return EFI_INVALID_PARAMETER;
  Input = gST->ConIn;
  Output = gST->ConOut;
  if ((Input == NULL) || (Output == NULL) || (Input->WaitForKey == NULL))
  {
    return EFI_UNSUPPORTED;
  }

  Status = Input->Reset(Input, FALSE);
  if (EFI_ERROR(Status))
  {
    return Status;
  }

  ResetReturned = FALSE;
  Rescan = TRUE;
  for (;;)
  {
    if (Rescan)
    {
      AcFreeBootEntries(Entries, EntryCount);
      Entries = NULL;
      EntryCount = 0;
      Status = AcScanBootEntries(&Entries, &EntryCount);
      if (EFI_ERROR(Status))
        break;
      Rescan = FALSE;
    }
    Status =
      InternalShowBootMenu(Input, Output, Entries, EntryCount, ResetReturned, &Action, &Selected);
    if (EFI_ERROR(Status))
      break;

    if (Action == AcBootMenuReboot)
    {
      (VOID) Output->OutputString(Output, L"\r\nRebooting...\r\n");
      gRT->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
      ResetReturned = TRUE;
      continue;
    }
    ResolveStatus = AcResolveBootResources(&Entries[Selected], &Resources);
    KernelStatus = EFI_NOT_STARTED;
    if (!EFI_ERROR(ResolveStatus))
      KernelStatus =
        InternalLoadBootKernel(ImageHandle, &Entries[Selected], &Resources, Config, &Kernel);
    Status = InternalShowBootResources(Input, Output, &Entries[Selected], ResolveStatus, &Resources,
                                       KernelStatus, &Kernel);
    InternalFreeBootKernel(&Kernel);
    AcFreeBootResources(&Resources);
    if (EFI_ERROR(Status))
      break;
    Rescan =
      EFI_ERROR(ResolveStatus) || KernelStatus == EFI_MEDIA_CHANGED || KernelStatus == EFI_NO_MEDIA;
  }
  AcFreeBootEntries(Entries, EntryCount);
  return Status;
}
