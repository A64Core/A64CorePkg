/** @file
  Built-in text menu and reboot handling.
**/

#include <Uefi.h>

#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

EFI_STATUS
EFIAPI
UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *Input = gST->ConIn;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *Output = gST->ConOut;
  EFI_INPUT_KEY                    Key;
  EFI_STATUS                       Status;
  UINTN                            EventIndex;

  if (Input == NULL || Output == NULL || Input->WaitForKey == NULL)
    return EFI_UNSUPPORTED;
  Status = Input->Reset(Input, FALSE);
  if (EFI_ERROR(Status))
    return Status;
  Status = Output->ClearScreen(Output);
  if (EFI_ERROR(Status))
    return Status;
  Status = Output->OutputString(Output, L"A64Core\r\n\r\n> Reboot\r\n\r\nEnter: Reboot\r\n");
  if (EFI_ERROR(Status))
    return Status;
  for (;;)
  {
    Status = gBS->WaitForEvent(1, &Input->WaitForKey, &EventIndex);
    if (EFI_ERROR(Status))
      return Status;
    Status = Input->ReadKeyStroke(Input, &Key);
    if (Status == EFI_NOT_READY)
      continue;
    if (EFI_ERROR(Status))
      return Status;
    if (Key.UnicodeChar != L'\r')
      continue;
    (VOID) Output->OutputString(Output, L"\r\nRebooting...\r\n");
    gRT->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
    return EFI_DEVICE_ERROR;
  }
}
