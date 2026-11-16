/** @file
  Bounded UEFI variable capture and owned Apple boot data.
**/

#include "NvramInternal.h"

CONST EFI_GUID gAcAppleNvramGuid = {
  0x7C436110, 0xAB2A, 0x4BBB, {0xA8, 0x80, 0xFE, 0x41, 0x99, 0x5C, 0x9F, 0x82}};
CONST EFI_GUID gAcAppleSystemNvramGuid = {
  0x40A0DDD2, 0x77F8, 0x4392, {0xB4, 0xA3, 0x1E, 0x73, 0x04, 0x20, 0x65, 0x16}};

STATIC VOID
FreeVariables(AC_NVRAM_VARIABLE *Variable)
{
  AC_NVRAM_VARIABLE *Next;

  while (Variable != NULL)
  {
    Next = Variable->Next;
    if (Variable->Name != NULL)
      FreePool(Variable->Name);
    if (Variable->Data != NULL)
    {
      ZeroMem(Variable->Data, Variable->Size);
      FreePool(Variable->Data);
    }
    FreePool(Variable);
    Variable = Next;
  }
}

STATIC EFI_STATUS
EncodeName(CONST CHAR16 *Name, UINTN Units, CHAR8 **Encoded)
{
  CHAR8 *Result;
  UINTN  Used = 0;
  UINT32 Code;

  *Encoded = NULL;
  Result = AllocateZeroPool(Units * 3);
  if (Result == NULL)
    return EFI_OUT_OF_RESOURCES;
  for (UINTN Index = 0; Index + 1 < Units; ++Index)
  {
    Code = Name[Index];
    if (Code >= 0xD800 && Code <= 0xDBFF && Index + 2 < Units && Name[Index + 1] >= 0xDC00 &&
        Name[Index + 1] <= 0xDFFF)
      Code = 0x10000 + ((Code - 0xD800) << 10) + (Name[++Index] - 0xDC00);
    else if (Code >= 0xD800 && Code <= 0xDFFF)
    {
      FreePool(Result);
      return EFI_COMPROMISED_DATA;
    }
    if (Code < 0x80)
      Result[Used++] = (CHAR8)Code;
    else if (Code < 0x800)
    {
      Result[Used++] = (CHAR8)(0xC0 | (Code >> 6));
      Result[Used++] = (CHAR8)(0x80 | (Code & 0x3F));
    }
    else
    {
      if (Code >= 0x10000)
      {
        Result[Used++] = (CHAR8)(0xF0 | (Code >> 18));
        Result[Used++] = (CHAR8)(0x80 | ((Code >> 12) & 0x3F));
      }
      else
        Result[Used++] = (CHAR8)(0xE0 | (Code >> 12));
      Result[Used++] = (CHAR8)(0x80 | ((Code >> 6) & 0x3F));
      Result[Used++] = (CHAR8)(0x80 | (Code & 0x3F));
    }
  }
  *Encoded = Result;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ReadValue(CONST CHAR16 *Name, AC_NVRAM_VARIABLE *Variable)
{
  EFI_GUID   Guid;
  UINTN      Capacity = 0;
  UINTN      Size;
  EFI_STATUS Status;

  for (UINTN Retry = 0; Retry < 8; ++Retry)
  {
    Size = Capacity;
    Guid = Variable->Guid;
    Status = gRT->GetVariable((CHAR16 *)Name, &Guid, &Variable->Attributes, &Size, Variable->Data);
    if (Status == EFI_NOT_FOUND)
      return EFI_ABORTED;
    if (Status == EFI_SUCCESS)
    {
      if (Size > Capacity)
        return EFI_COMPROMISED_DATA;
      Variable->Size = Size;
      return EFI_SUCCESS;
    }
    if (Status != EFI_BUFFER_TOO_SMALL)
      return Status;
    if (Size <= Capacity || Size > AC_NVRAM_MAX_VALUE)
      return EFI_BAD_BUFFER_SIZE;
    if (Variable->Data != NULL)
    {
      ZeroMem(Variable->Data, Capacity);
      FreePool(Variable->Data);
      Variable->Data = NULL;
    }
    Variable->Data = AllocateZeroPool(Size);
    if (Variable->Data == NULL)
      return EFI_OUT_OF_RESOURCES;
    Capacity = Size;
    Variable->Size = Capacity;
  }
  return EFI_ABORTED;
}

STATIC EFI_STATUS
CollectVariables(AC_NVRAM_VARIABLE **Variables)
{
  CHAR16             *Name = NULL;
  CHAR16             *Cursor = NULL;
  UINTN               Capacity = 128;
  UINTN               CursorSize = sizeof(CHAR16);
  UINTN               Size;
  UINTN               Units;
  UINTN               Count = 0;
  UINTN               Total = 0;
  EFI_GUID            CursorGuid = {0};
  EFI_GUID            Guid;
  EFI_STATUS          Status = EFI_ABORTED;
  AC_NVRAM_VARIABLE  *Variable = NULL;
  AC_NVRAM_VARIABLE **Link;
  INTN                Order;

  *Variables = NULL;
  Name = AllocateZeroPool(Capacity);
  Cursor = AllocateZeroPool(AC_NVRAM_MAX_NAME_UNITS * sizeof(*Cursor));
  if (Name == NULL || Cursor == NULL)
  {
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }
  for (UINTN Step = 0; Step < 65536; ++Step)
  {
    CopyMem(Name, Cursor, CursorSize);
    Guid = CursorGuid;
    Size = Capacity;
    Status = gRT->GetNextVariableName(&Size, Name, &Guid);
    if (Status == EFI_NOT_FOUND)
    {
      Status = EFI_SUCCESS;
      goto Done;
    }
    if (Status == EFI_INVALID_PARAMETER)
    {
      Status = EFI_ABORTED;
      goto Done;
    }
    if (Status == EFI_BUFFER_TOO_SMALL)
    {
      if (Size <= Capacity || Size > AC_NVRAM_MAX_NAME_UNITS * sizeof(*Name) ||
          Size % sizeof(*Name) != 0)
      {
        Status = EFI_BAD_BUFFER_SIZE;
        goto Done;
      }
      FreePool(Name);
      Capacity = Size;
      Name = AllocateZeroPool(Capacity);
      if (Name == NULL)
      {
        Status = EFI_OUT_OF_RESOURCES;
        goto Done;
      }
      continue;
    }
    if (EFI_ERROR(Status))
      goto Done;
    if (Size < 2 * sizeof(*Name) || Size > Capacity || Size % sizeof(*Name) != 0)
    {
      Status = EFI_COMPROMISED_DATA;
      goto Done;
    }
    Units = 0;
    while (Units < Size / sizeof(*Name) && Name[Units] != 0)
      ++Units;
    if (Units == 0 || Units == Size / sizeof(*Name))
    {
      Status = EFI_COMPROMISED_DATA;
      goto Done;
    }
    Size = (Units + 1) * sizeof(*Name);
    if (Size == CursorSize && CompareMem(&Guid, &CursorGuid, sizeof(Guid)) == 0 &&
        CompareMem(Name, Cursor, Size) == 0)
    {
      Status = EFI_ABORTED;
      goto Done;
    }
    CopyMem(Cursor, Name, Size);
    CursorSize = Size;
    CursorGuid = Guid;
    if (CompareMem(&Guid, &gAcAppleNvramGuid, sizeof(Guid)) != 0 &&
        CompareMem(&Guid, &gAcAppleSystemNvramGuid, sizeof(Guid)) != 0)
      continue;
    if (++Count > AC_NVRAM_MAX_VARIABLES)
    {
      Status = EFI_BAD_BUFFER_SIZE;
      goto Done;
    }
    Variable = AllocateZeroPool(sizeof(*Variable));
    if (Variable == NULL)
    {
      Status = EFI_OUT_OF_RESOURCES;
      goto Done;
    }
    Variable->Guid = Guid;
    Status = EncodeName(Name, Units + 1, &Variable->Name);
    if (!EFI_ERROR(Status))
      Status = ReadValue(Name, Variable);
    if (EFI_ERROR(Status))
    {
      DEBUG((DEBUG_ERROR, "ACNV: Read %g:%s: %r\n", &Guid, Name, Status));
      goto Done;
    }
    Size = sizeof(*Variable) + AsciiStrLen(Variable->Name) + 1 + Variable->Size;
    if (Size > AC_NVRAM_MAX_STORE - Total)
    {
      Status = EFI_BAD_BUFFER_SIZE;
      goto Done;
    }
    Total += Size;
    for (Link = Variables; *Link != NULL; Link = &(*Link)->Next)
    {
      Order = CompareMem(&Guid, &(*Link)->Guid, sizeof(Guid));
      if (Order == 0)
        Order = AsciiStrCmp(Variable->Name, (*Link)->Name);
      if (Order == 0)
      {
        Status = EFI_ABORTED;
        goto Done;
      }
      if (Order < 0)
        break;
    }
    Variable->Next = *Link;
    *Link = Variable;
    Variable = NULL;
    Status = EFI_ABORTED;
  }
  Status = EFI_ABORTED;

Done:
  FreeVariables(Variable);
  if (Name != NULL)
    FreePool(Name);
  if (Cursor != NULL)
    FreePool(Cursor);
  if (EFI_ERROR(Status))
  {
    FreeVariables(*Variables);
    *Variables = NULL;
  }
  return Status;
}

STATIC BOOLEAN
SameVariables(CONST AC_NVRAM_VARIABLE *First, CONST AC_NVRAM_VARIABLE *Second)
{
  while (First != NULL && Second != NULL)
  {
    if (CompareMem(&First->Guid, &Second->Guid, sizeof(First->Guid)) != 0 ||
        AsciiStrCmp(First->Name, Second->Name) != 0 || First->Size != Second->Size ||
        First->Attributes != Second->Attributes ||
        (First->Size != 0 && CompareMem(First->Data, Second->Data, First->Size) != 0))
      return FALSE;
    First = First->Next;
    Second = Second->Next;
  }
  return First == NULL && Second == NULL;
}

EFI_STATUS
AcAppleNvramCreate(OUT AC_APPLE_NVRAM_CONTEXT **Context)
{
  AC_APPLE_NVRAM_CONTEXT *Result;
  AC_NVRAM_VARIABLE      *Second = NULL;
  EFI_STATUS              Status = EFI_ABORTED;

  if (Context == NULL)
    return EFI_INVALID_PARAMETER;
  *Context = NULL;
  if (gRT == NULL || gRT->GetVariable == NULL || gRT->GetNextVariableName == NULL)
    return EFI_UNSUPPORTED;
  Result = AllocateZeroPool(sizeof(*Result));
  if (Result == NULL)
    return EFI_OUT_OF_RESOURCES;
  for (UINTN Attempt = 0; Attempt < AC_NVRAM_RETRIES; ++Attempt)
  {
    Status = CollectVariables(&Result->Variables);
    if (!EFI_ERROR(Status))
      Status = CollectVariables(&Second);
    if (!EFI_ERROR(Status) && !SameVariables(Result->Variables, Second))
      Status = EFI_ABORTED;
    FreeVariables(Second);
    Second = NULL;
    if (Status != EFI_ABORTED)
      break;
    FreeVariables(Result->Variables);
    Result->Variables = NULL;
  }
  if (!EFI_ERROR(Status))
    Status = InternalNvramEncodeV3(Result);
  if (EFI_ERROR(Status))
  {
    AcAppleNvramFree(Result);
    return Status;
  }
  *Context = Result;
  return EFI_SUCCESS;
}

VOID
AcAppleNvramFree(IN AC_APPLE_NVRAM_CONTEXT *Context OPTIONAL)
{
  if (Context == NULL)
    return;
  FreeVariables(Context->Variables);
  if (Context->Info.Data != NULL)
  {
    ZeroMem((VOID *)Context->Info.Data, Context->Info.Size);
    FreePool((VOID *)Context->Info.Data);
  }
  FreePool(Context);
}

EFI_STATUS
AcAppleNvramGetInfo(IN CONST AC_APPLE_NVRAM_CONTEXT *Context, OUT AC_APPLE_NVRAM_INFO *Info)
{
  if (Info == NULL)
    return EFI_INVALID_PARAMETER;
  ZeroMem(Info, sizeof(*Info));
  if (Context == NULL)
    return EFI_INVALID_PARAMETER;
  *Info = Context->Info;
  return EFI_SUCCESS;
}

EFI_STATUS
AcAppleNvramGetVariable(IN CONST AC_APPLE_NVRAM_CONTEXT *Context, IN CONST EFI_GUID *Guid,
                        IN CONST CHAR8 *Name, OUT CONST VOID **Data, OUT UINTN *Size)
{
  if (Data == NULL || Size == NULL)
    return EFI_INVALID_PARAMETER;
  *Data = NULL;
  *Size = 0;
  if (Context == NULL || Guid == NULL || Name == NULL || *Name == '\0')
    return EFI_INVALID_PARAMETER;
  for (CONST AC_NVRAM_VARIABLE *Variable = Context->Variables; Variable != NULL;
       Variable = Variable->Next)
    if (CompareMem(&Variable->Guid, Guid, sizeof(*Guid)) == 0 &&
        AsciiStrCmp(Variable->Name, Name) == 0)
    {
      *Data = Variable->Data;
      *Size = Variable->Size;
      return EFI_SUCCESS;
    }
  return EFI_NOT_FOUND;
}
