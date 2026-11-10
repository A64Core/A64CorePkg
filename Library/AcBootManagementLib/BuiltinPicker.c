/** @file
  Built-in text selection and pre-handoff error display.
**/

#include "BootManagementInternal.h"

STATIC EFI_STATUS
ReadKey(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *Input, OUT EFI_INPUT_KEY *Key)
{
  EFI_STATUS Status;
  UINTN      EventIndex;

  for (;;)
  {
    Status = gBS->WaitForEvent(1, &Input->WaitForKey, &EventIndex);
    if (EFI_ERROR(Status))
    {
      return Status;
    }

    Status = Input->ReadKeyStroke(Input, Key);
    if (Status != EFI_NOT_READY)
      return Status;
  }
}

STATIC EFI_STATUS
OutputLine(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *Output, CONST CHAR16 *Text, UINTN Width)
{
  CHAR16     Line[256];
  UINTN      Index;
  EFI_STATUS Status;

  Width = MIN(Width, ARRAY_SIZE(Line) - 1);
  for (Index = 0; Index < Width && Text[Index] != L'\0'; ++Index)
    Line[Index] = Text[Index] < L' ' || Text[Index] == 0x7F ? L' ' : Text[Index];
  if (Index != 0 && Line[Index - 1] >= 0xD800 && Line[Index - 1] <= 0xDBFF)
    --Index;
  Line[Index] = L'\0';
  Status = Output->OutputString(Output, Line);
  if (EFI_ERROR(Status))
    return Status;
  return Output->OutputString(Output, L"\r\n");
}

STATIC EFI_STATUS
WaitForEnter(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *Input)
{
  EFI_INPUT_KEY Key;
  EFI_STATUS    Status;

  do
  {
    Status = ReadKey(Input, &Key);
    if (EFI_ERROR(Status))
      return Status;
  } while (Key.UnicodeChar != L'\r');
  return EFI_SUCCESS;
}

EFI_STATUS
InternalShowBootMenu(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *Input, EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *Output,
                     CONST AC_BOOT_ENTRY *Entries, UINTN EntryCount, BOOLEAN ResetReturned,
                     OUT AC_BOOT_MENU_ACTION *Action, OUT UINTN *SelectedIndex)
{
  EFI_INPUT_KEY Key;
  EFI_STATUS    Status;
  UINTN         Selected = 0;
  UINTN         Columns = 80;
  UINTN         Rows = 25;
  UINTN         Page;
  UINTN         First;
  UINTN         Last;
  UINTN         Index;
  CHAR16        Line[256];
  CHAR16       *Source;

  if (Output->Mode != NULL && Output->Mode->Mode >= 0)
  {
    Status = Output->QueryMode(Output, (UINTN)Output->Mode->Mode, &Columns, &Rows);
    if (EFI_ERROR(Status))
      return Status;
  }
  if (Columns < 20 || Rows < 10)
    return EFI_UNSUPPORTED;
  Page = Rows - 8;
  for (;;)
  {
    Status = Output->ClearScreen(Output);
    if (EFI_ERROR(Status))
      return Status;
    Status = Output->OutputString(Output, L"A64Core\r\n\r\n");
    if (EFI_ERROR(Status))
      return Status;
    First = Selected / Page * Page;
    Last = First + MIN(Page, EntryCount + 1 - First);
    for (Index = First; Index < Last; ++Index)
    {
      UnicodeSPrint(Line, sizeof(Line), L"%s%u. %s", Index == Selected ? L"> " : L"  ",
                    (UINT32)(Index + 1), Index == EntryCount ? L"Reboot" : Entries[Index].Name);
      Status = OutputLine(Output, Line, Columns - 1);
      if (EFI_ERROR(Status))
        return Status;
    }
    Status = OutputLine(Output, L"", Columns - 1);
    if (!EFI_ERROR(Status))
      Status = OutputLine(Output, L"Up/Down: Move", Columns - 1);
    if (!EFI_ERROR(Status))
      Status = OutputLine(Output, L"Enter: Boot / Reboot", Columns - 1);
    if (EFI_ERROR(Status))
      return Status;
    if (Selected < EntryCount)
    {
      Source = ConvertDevicePathToText(Entries[Selected].DevicePath, FALSE, FALSE);
      if (Source == NULL)
        return EFI_OUT_OF_RESOURCES;
      Status = OutputLine(Output, Source, Columns - 1);
      FreePool(Source);
      if (EFI_ERROR(Status))
        return Status;
    }
    if (ResetReturned)
    {
      Status = OutputLine(Output, L"ResetSystem returned unexpectedly.", Columns - 1);
      if (EFI_ERROR(Status))
        return Status;
    }
    Status = ReadKey(Input, &Key);
    if (EFI_ERROR(Status))
      return Status;
    if (Key.ScanCode == SCAN_UP)
      Selected = Selected == 0 ? EntryCount : Selected - 1;
    else if (Key.ScanCode == SCAN_DOWN)
      Selected = Selected == EntryCount ? 0 : Selected + 1;
    else if (Key.UnicodeChar == L'\r')
    {
      *Action = Selected == EntryCount ? AcBootMenuReboot : AcBootMenuSelect;
      *SelectedIndex = Selected;
      return EFI_SUCCESS;
    }
  }
}

EFI_STATUS
InternalShowBootResources(EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *Input,
                          EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *Output, CONST AC_BOOT_ENTRY *Entry,
                          EFI_STATUS ResolveStatus, CONST AC_BOOT_RESOURCES *Resources,
                          EFI_STATUS KernelStatus, CONST AC_BOOT_KERNEL *Kernel)
{
  CHAR16    *Source;
  CHAR16     Message[256];
  EFI_STATUS Status;

  Status = Output->ClearScreen(Output);
  if (EFI_ERROR(Status))
    return Status;
  Status = OutputLine(Output, Entry->Name, 255);
  if (EFI_ERROR(Status))
    return Status;
  Source = ConvertDevicePathToText(Entry->DevicePath, FALSE, FALSE);
  if (Source == NULL)
    return EFI_OUT_OF_RESOURCES;
  Status = Output->OutputString(Output, L"\r\nSource:\r\n");
  if (!EFI_ERROR(Status))
    Status = Output->OutputString(Output, Source);
  FreePool(Source);
  if (EFI_ERROR(Status))
    return Status;
  UnicodeSPrint(Message, sizeof(Message), L"\r\n\r\nResource location: %r\r\n", ResolveStatus);
  Status = Output->OutputString(Output, Message);
  if (EFI_ERROR(Status))
    return Status;
  if (!EFI_ERROR(ResolveStatus))
  {
    Status = Output->OutputString(Output, L"\r\nBoot directory:\r\n");
    if (!EFI_ERROR(Status))
      Status = Output->OutputString(Output, Resources->BootDirectory);
    if (!EFI_ERROR(Status))
      Status = Output->OutputString(Output, L"\r\n\r\nKernel:\r\n");
    if (!EFI_ERROR(Status))
      Status = Output->OutputString(Output, Resources->KernelPath);
    if (EFI_ERROR(Status))
      return Status;
    UnicodeSPrint(Message, sizeof(Message), L"\r\n\r\nKernel boot: %r\r\n", KernelStatus);
    Status = Output->OutputString(Output, Message);
    if (EFI_ERROR(Status))
      return Status;
    if (EFI_ERROR(KernelStatus) && Kernel->Stage != NULL)
    {
      Status = OutputLine(Output, Kernel->Stage, 255);
      if (EFI_ERROR(Status))
        return Status;
    }
  }
  Status = Output->OutputString(Output, L"\r\nXNU has not been started. Enter: Return\r\n");
  if (EFI_ERROR(Status))
    return Status;
  return WaitForEnter(Input);
}
