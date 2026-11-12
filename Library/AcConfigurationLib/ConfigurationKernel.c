/** @file
  Kernel patch configuration mapping and ownership.
**/

#include "ConfigurationInternal.h"

VOID
InternalConfigurationFreePatch(IN OUT AC_KERNEL_PATCH_ENTRY *Patch)
{
  if (Patch->Identifier != NULL)
    FreePool(Patch->Identifier);
  if (Patch->Base != NULL)
    FreePool(Patch->Base);
  if (Patch->Comment != NULL)
    FreePool(Patch->Comment);
  if (Patch->Find.Data != NULL)
    FreePool(Patch->Find.Data);
  if (Patch->Replace.Data != NULL)
    FreePool(Patch->Replace.Data);
  if (Patch->Mask.Data != NULL)
    FreePool(Patch->Mask.Data);
  if (Patch->ReplaceMask.Data != NULL)
    FreePool(Patch->ReplaceMask.Data);
  ZeroMem(Patch, sizeof(*Patch));
}

STATIC EFI_STATUS
ParseVersion(CONST AC_PLIST_NODE *Node, UINT32 *Version)
{
  CONST CHAR8 *Text;
  UINT32       Result = 0;
  UINT32       Part;
  UINT32       Index;
  UINT32       Digits;

  *Version = 0;
  Text = AcPlistGetString(Node);
  if (Text == NULL)
    return EFI_INVALID_PARAMETER;
  if (*Text == '\0')
    return EFI_SUCCESS;
  for (Index = 0; Index < 3; ++Index)
  {
    Part = Digits = 0;
    while (*Text >= '0' && *Text <= '9')
    {
      if (++Digits > 2)
        return EFI_COMPROMISED_DATA;
      Part = Part * 10 + (UINT32)(*Text++ - '0');
    }
    if (Digits == 0)
      return EFI_COMPROMISED_DATA;
    Result = Result * 100 + Part;
    if (*Text == '\0')
    {
      while (++Index < 3)
        Result *= 100;
      *Version = Result;
      return EFI_SUCCESS;
    }
    if (*Text++ != '.')
      return EFI_COMPROMISED_DATA;
  }
  return EFI_COMPROMISED_DATA;
}

STATIC EFI_STATUS
ParsePatch(CONST AC_PLIST_NODE *Node, UINTN Index, AC_KERNEL_PATCH_ENTRY *Patch, UINTN *Issues)
{
  CONST AC_PLIST_NODE *Value;
  CONST CHAR8         *Key;
  CONST CHAR8         *Text;
  CHAR8              **StringField;
  AC_CONFIG_DATA      *DataField;
  UINT32              *IntegerField;
  UINT32              *VersionField;
  UINT64               Number;
  UINTN                Field;
  UINTN                Length;
  EFI_STATUS           Status;
  BOOLEAN              Valid = TRUE;

  for (Field = 0; Field < AcPlistGetCount(Node); ++Field)
  {
    AcPlistGetDictionaryItem(Node, Field, &Key, &Value);
    if (Key[0] == '#')
      continue;
    StringField = NULL;
    DataField = NULL;
    IntegerField = VersionField = NULL;
    Status = EFI_SUCCESS;
    if (AsciiStrCmp(Key, "Identifier") == 0)
      StringField = &Patch->Identifier;
    else if (AsciiStrCmp(Key, "Base") == 0)
      StringField = &Patch->Base;
    else if (AsciiStrCmp(Key, "Comment") == 0)
      StringField = &Patch->Comment;
    else if (AsciiStrCmp(Key, "Find") == 0)
      DataField = &Patch->Find;
    else if (AsciiStrCmp(Key, "Replace") == 0)
      DataField = &Patch->Replace;
    else if (AsciiStrCmp(Key, "Mask") == 0)
      DataField = &Patch->Mask;
    else if (AsciiStrCmp(Key, "ReplaceMask") == 0)
      DataField = &Patch->ReplaceMask;
    else if (AsciiStrCmp(Key, "Count") == 0)
      IntegerField = &Patch->Count;
    else if (AsciiStrCmp(Key, "Skip") == 0)
      IntegerField = &Patch->Skip;
    else if (AsciiStrCmp(Key, "Limit") == 0)
      IntegerField = &Patch->Limit;
    else if (AsciiStrCmp(Key, "MinKernel") == 0)
      VersionField = &Patch->MinKernel;
    else if (AsciiStrCmp(Key, "MaxKernel") == 0)
      VersionField = &Patch->MaxKernel;
    else if (AsciiStrCmp(Key, "Enabled") == 0)
      Status = AcPlistGetBoolean(Value, &Patch->Enabled);
    else
    {
      InternalConfigurationIssue(Issues, "Kernel.Patch", Index, Key, "unsupported key");
      continue;
    }
    if (StringField != NULL)
    {
      Text = AcPlistGetString(Value);
      if (Text == NULL)
        Status = EFI_INVALID_PARAMETER;
      else if ((Length = AsciiStrLen(Text)) != 0)
      {
        *StringField = AllocateZeroPool(Length + 1);
        if (*StringField == NULL)
          return EFI_OUT_OF_RESOURCES;
        CopyMem(*StringField, Text, Length);
      }
    }
    else if (DataField != NULL)
      Status = AcPlistDecodeData(Value, &DataField->Data, &DataField->Size);
    else if (VersionField != NULL)
      Status = ParseVersion(Value, VersionField);
    else if (IntegerField != NULL)
    {
      Status = AcPlistGetUnsignedInteger(Value, &Number);
      if (!EFI_ERROR(Status))
      {
        if (Number > MAX_UINT32)
          Status = EFI_BAD_BUFFER_SIZE;
        else
          *IntegerField = (UINT32)Number;
      }
    }
    if (Status == EFI_OUT_OF_RESOURCES)
      return Status;
    if (EFI_ERROR(Status))
    {
      InternalConfigurationIssue(Issues, "Kernel.Patch", Index, Key, "invalid field value");
      Valid = FALSE;
    }
  }
  if (Patch->Enabled &&
      (Patch->Identifier == NULL || Patch->Replace.Size == 0 || Patch->Replace.Size > MAX_UINT32 ||
       (Patch->Find.Size == 0 && Patch->Base == NULL) ||
       (Patch->Find.Size != 0 && Patch->Find.Size != Patch->Replace.Size) ||
       (Patch->Mask.Size != 0 && Patch->Mask.Size != Patch->Replace.Size) ||
       (Patch->ReplaceMask.Size != 0 && Patch->ReplaceMask.Size != Patch->Replace.Size) ||
       (Patch->MaxKernel != 0 && Patch->MinKernel > Patch->MaxKernel)))
  {
    InternalConfigurationIssue(Issues, "Kernel.Patch", Index, "", "invalid patch");
    Valid = FALSE;
  }
  if (!Valid)
    Patch->Enabled = FALSE;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ParsePatches(CONST AC_PLIST_NODE *Node, AC_KERNEL_CONFIG *Kernel, UINTN *Issues)
{
  CONST AC_PLIST_NODE *Value;
  EFI_STATUS           Status;
  UINTN                Count;
  UINTN                Index;

  if (AcPlistGetType(Node) != AcPlistTypeArray)
  {
    InternalConfigurationIssue(Issues, "Kernel.", MAX_UINTN, "Patch", "expected an array");
    return EFI_SUCCESS;
  }
  Count = AcPlistGetCount(Node);
  if (Count == 0)
    return EFI_SUCCESS;
  if (Count > MAX_UINTN / sizeof(*Kernel->Patches))
    return EFI_OUT_OF_RESOURCES;
  Kernel->Patches = AllocateZeroPool(Count * sizeof(*Kernel->Patches));
  if (Kernel->Patches == NULL)
    return EFI_OUT_OF_RESOURCES;
  Kernel->PatchCount = Count;
  for (Index = 0; Index < Count; ++Index)
  {
    Value = AcPlistGetArrayItem(Node, Index);
    if (AcPlistGetType(Value) != AcPlistTypeDictionary)
    {
      InternalConfigurationIssue(Issues, "Kernel.Patch", Index, "", "expected a dictionary");
      continue;
    }
    Status = ParsePatch(Value, Index, &Kernel->Patches[Index], Issues);
    if (EFI_ERROR(Status))
      return Status;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
InternalConfigurationParseKernel(IN CONST AC_PLIST_NODE *Node, IN OUT AC_KERNEL_CONFIG *Kernel,
                                 IN OUT UINTN *IssueCount)
{
  CONST AC_PLIST_NODE *Value;
  CONST CHAR8         *Key;
  EFI_STATUS           Status;
  UINTN                Index;

  if (AcPlistGetType(Node) != AcPlistTypeDictionary)
  {
    InternalConfigurationIssue(IssueCount, "", MAX_UINTN, "Kernel", "expected a dictionary");
    return EFI_SUCCESS;
  }
  for (Index = 0; Index < AcPlistGetCount(Node); ++Index)
  {
    AcPlistGetDictionaryItem(Node, Index, &Key, &Value);
    if (Key[0] == '#')
      continue;
    if (AsciiStrCmp(Key, "Patch") == 0)
    {
      Status = ParsePatches(Value, Kernel, IssueCount);
      if (EFI_ERROR(Status))
        return Status;
    }
    else
      InternalConfigurationIssue(IssueCount, "Kernel.", MAX_UINTN, Key, "unsupported key");
  }
  return EFI_SUCCESS;
}
