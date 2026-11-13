/** @file
  Configured NVRAM deletion and addition.
**/

#include "MainInternal.h"

#include <Library/UefiRuntimeServicesTableLib.h>

STATIC EFI_STATUS
MatchesAdd(CONST AC_NVRAM_ADD_ENTRY *Entry, BOOLEAN *Matches)
{
  EFI_GUID   Guid = Entry->Guid;
  UINTN      Size = 0;
  VOID      *Current;
  EFI_STATUS Status;

  *Matches = FALSE;
  Status = gRT->GetVariable(Entry->Name, &Guid, NULL, &Size, NULL);
  if (Status == EFI_NOT_FOUND || (Status == EFI_SUCCESS && Size == 0))
  {
    *Matches = Entry->Value.Size == 0;
    return EFI_SUCCESS;
  }
  if (Status != EFI_BUFFER_TOO_SMALL || Size != Entry->Value.Size)
    return EFI_SUCCESS;
  Current = AllocatePool(Size);
  if (Current == NULL)
    return EFI_OUT_OF_RESOURCES;
  Status = gRT->GetVariable(Entry->Name, &Guid, NULL, &Size, Current);
  if (!EFI_ERROR(Status) && Size == Entry->Value.Size)
    *Matches = CompareMem(Current, Entry->Value.Data, Size) == 0;
  FreePool(Current);
  return EFI_SUCCESS;
}

EFI_STATUS
AcApplyNvram(IN CONST AC_GLOBAL_CONFIG *Config)
{
  CONST AC_NVRAM_CONFIG       *Nvram;
  CONST AC_NVRAM_DELETE_ENTRY *Delete;
  CONST AC_NVRAM_ADD_ENTRY    *Add;
  EFI_GUID                     Guid;
  EFI_STATUS                   Status;
  UINTN                        Index;
  UINTN                        Other;
  UINTN                        Size;
  BOOLEAN                      Matches;

  if (Config == NULL)
    return EFI_INVALID_PARAMETER;
  Nvram = &Config->Nvram;
  if (Nvram->DeleteCount == 0 && Nvram->AddCount == 0)
    return EFI_SUCCESS;
  if (gRT == NULL || gRT->GetVariable == NULL || gRT->SetVariable == NULL)
    return EFI_UNSUPPORTED;
  for (Index = 0; Index < Nvram->DeleteCount; ++Index)
  {
    Delete = &Nvram->Delete[Index];
    Matches = FALSE;
    for (Other = 0; Other < Nvram->AddCount; ++Other)
    {
      Add = &Nvram->Add[Other];
      if (CompareMem(&Delete->Guid, &Add->Guid, sizeof(EFI_GUID)) == 0 &&
          StrCmp(Delete->Name, Add->Name) == 0)
      {
        Status = MatchesAdd(Add, &Matches);
        if (EFI_ERROR(Status))
          return Status;
        break;
      }
    }
    if (Matches)
      continue;
    Guid = Delete->Guid;
    Status = gRT->SetVariable(Delete->Name, &Guid, 0, 0, NULL);
    DEBUG((EFI_ERROR(Status) && Status != EFI_NOT_FOUND ? DEBUG_WARN : DEBUG_INFO,
           "AC: NVRAM.Delete %g:%s: %r\n", &Guid, Delete->Name, Status));
    if (EFI_ERROR(Status) && Status != EFI_NOT_FOUND)
      InternalMainReport(L"Delete NVRAM variable", Delete->Name, Status);
  }
  for (Index = 0; Index < Nvram->AddCount; ++Index)
  {
    Add = &Nvram->Add[Index];
    Guid = Add->Guid;
    Size = 0;
    Status = gRT->GetVariable(Add->Name, &Guid, NULL, &Size, NULL);
    if (Status == EFI_BUFFER_TOO_SMALL || Status == EFI_SUCCESS)
      continue;
    Status = gRT->SetVariable(Add->Name, &Guid,
                              EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                              Add->Value.Size, Add->Value.Data);
    DEBUG((EFI_ERROR(Status) && Add->Value.Size != 0 ? DEBUG_WARN : DEBUG_INFO,
           "AC: NVRAM.Add %g:%s: %r\n", &Guid, Add->Name, Status));
    if (EFI_ERROR(Status) && Add->Value.Size != 0)
      InternalMainReport(L"Add NVRAM variable", Add->Name, Status);
  }
  return EFI_SUCCESS;
}
