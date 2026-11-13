/** @file
  NVRAM configuration mapping and value encoding.
**/

#include "ConfigurationInternal.h"

STATIC BOOLEAN
ParseGuid(CONST CHAR8 *Text, EFI_GUID *Guid)
{
  UINT8 Bytes[16];
  UINTN Index;
  UINTN Used = 0;
  UINT8 Value;
  UINT8 Digit;

  if (AsciiStrLen(Text) != 36)
    return FALSE;
  for (Index = 0; Index < 36;)
  {
    if (Index == 8 || Index == 13 || Index == 18 || Index == 23)
    {
      if (Text[Index++] != '-')
        return FALSE;
      continue;
    }
    Value = 0;
    for (UINTN Half = 0; Half < 2; ++Half)
    {
      Digit = (UINT8)Text[Index++];
      if (Digit >= '0' && Digit <= '9')
        Digit -= '0';
      else if (Digit >= 'a' && Digit <= 'f')
        Digit = (UINT8)(Digit - 'a' + 10);
      else if (Digit >= 'A' && Digit <= 'F')
        Digit = (UINT8)(Digit - 'A' + 10);
      else
        return FALSE;
      Value = (UINT8)((Value << 4) | Digit);
    }
    Bytes[Used++] = Value;
  }
  Guid->Data1 = (UINT32)Bytes[0] << 24 | (UINT32)Bytes[1] << 16 | (UINT32)Bytes[2] << 8 | Bytes[3];
  Guid->Data2 = (UINT16)((UINT16)Bytes[4] << 8 | Bytes[5]);
  Guid->Data3 = (UINT16)((UINT16)Bytes[6] << 8 | Bytes[7]);
  CopyMem(Guid->Data4, Bytes + 8, sizeof(Guid->Data4));
  return TRUE;
}

STATIC EFI_STATUS
CopyName(CONST CHAR8 *Text, CHAR16 **Name)
{
  UINTN Length = AsciiStrLen(Text);
  UINTN Index;

  *Name = NULL;
  if (Length == 0)
    return EFI_INVALID_PARAMETER;
  for (Index = 0; Index < Length; ++Index)
    if ((UINT8)Text[Index] < 0x20 || (UINT8)Text[Index] > 0x7E)
      return EFI_INVALID_PARAMETER;
  *Name = AllocateZeroPool((Length + 1) * sizeof(**Name));
  if (*Name == NULL)
    return EFI_OUT_OF_RESOURCES;
  for (Index = 0; Index <= Length; ++Index)
    (*Name)[Index] = (CHAR16)Text[Index];
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
EncodeValue(CONST AC_PLIST_NODE *Node, AC_CONFIG_DATA *Value)
{
  CONST CHAR8 *Text;
  UINT64       Number;
  BOOLEAN      Flag;
  EFI_STATUS   Status;

  switch (AcPlistGetType(Node))
  {
  case AcPlistTypeData:
    return AcPlistDecodeData(Node, &Value->Data, &Value->Size);
  case AcPlistTypeString:
    Text = AcPlistGetString(Node);
    Value->Size = AsciiStrLen(Text) + 1;
    Value->Data = AllocateZeroPool(Value->Size);
    if (Value->Data == NULL)
      return EFI_OUT_OF_RESOURCES;
    CopyMem(Value->Data, Text, Value->Size);
    return EFI_SUCCESS;
  case AcPlistTypeInteger:
    Status = AcPlistGetUnsignedInteger(Node, &Number);
    if (EFI_ERROR(Status))
      return Status;
    if (Number > MAX_UINT32)
      return EFI_BAD_BUFFER_SIZE;
    Value->Size = sizeof(UINT32);
    break;
  case AcPlistTypeBoolean:
    Status = AcPlistGetBoolean(Node, &Flag);
    if (EFI_ERROR(Status))
      return Status;
    Number = Flag ? 1 : 0;
    Value->Size = sizeof(UINT8);
    break;
  default:
    return EFI_UNSUPPORTED;
  }
  Value->Data = AllocateZeroPool(Value->Size);
  if (Value->Data == NULL)
    return EFI_OUT_OF_RESOURCES;
  for (UINTN Index = 0; Index < Value->Size; ++Index)
    Value->Data[Index] = (UINT8)(Number >> (Index * 8));
  return EFI_SUCCESS;
}

VOID
InternalConfigurationFreeNvram(IN OUT AC_NVRAM_CONFIG *Nvram)
{
  for (UINTN Index = 0; Index < Nvram->AddCount; ++Index)
  {
    if (Nvram->Add[Index].Name != NULL)
      FreePool(Nvram->Add[Index].Name);
    if (Nvram->Add[Index].Value.Data != NULL)
      FreePool(Nvram->Add[Index].Value.Data);
  }
  for (UINTN Index = 0; Index < Nvram->DeleteCount; ++Index)
    if (Nvram->Delete[Index].Name != NULL)
      FreePool(Nvram->Delete[Index].Name);
  if (Nvram->Add != NULL)
    FreePool(Nvram->Add);
  if (Nvram->Delete != NULL)
    FreePool(Nvram->Delete);
  ZeroMem(Nvram, sizeof(*Nvram));
}

STATIC EFI_STATUS
ParseEntries(CONST AC_PLIST_NODE *Node, AC_NVRAM_CONFIG *Nvram, BOOLEAN Add, UINTN *Issues)
{
  CONST AC_PLIST_NODE *Group;
  CONST AC_PLIST_NODE *Value;
  CONST CHAR8         *Key;
  CONST CHAR8         *Name;
  CONST CHAR8         *Scope = Add ? "NVRAM.Add" : "NVRAM.Delete";
  EFI_GUID             Guid;
  CHAR16              *Unicode;
  AC_CONFIG_DATA       Encoded;
  UINTN                Capacity = 0;
  UINTN                Index;
  UINTN                Item;
  EFI_STATUS           Status;

  if (AcPlistGetType(Node) != AcPlistTypeDictionary)
  {
    InternalConfigurationIssue(Issues, Scope, MAX_UINTN, "", "expected a dictionary");
    return EFI_SUCCESS;
  }
  for (Index = 0; Index < AcPlistGetCount(Node); ++Index)
  {
    AcPlistGetDictionaryItem(Node, Index, &Key, &Group);
    if (AcPlistGetCount(Group) > MAX_UINTN - Capacity)
      return EFI_OUT_OF_RESOURCES;
    Capacity += AcPlistGetCount(Group);
  }
  if (Capacity != 0)
  {
    if (Capacity > MAX_UINTN / (Add ? sizeof(*Nvram->Add) : sizeof(*Nvram->Delete)))
      return EFI_OUT_OF_RESOURCES;
    if (Add)
      Nvram->Add = AllocateZeroPool(Capacity * sizeof(*Nvram->Add));
    else
      Nvram->Delete = AllocateZeroPool(Capacity * sizeof(*Nvram->Delete));
    if ((Add ? (VOID *)Nvram->Add : (VOID *)Nvram->Delete) == NULL)
      return EFI_OUT_OF_RESOURCES;
  }
  for (Index = 0; Index < AcPlistGetCount(Node); ++Index)
  {
    AcPlistGetDictionaryItem(Node, Index, &Key, &Group);
    if (Key[0] == '#')
      continue;
    if (!ParseGuid(Key, &Guid) ||
        AcPlistGetType(Group) != (Add ? AcPlistTypeDictionary : AcPlistTypeArray))
    {
      InternalConfigurationIssue(Issues, Scope, Index, Key, "invalid GUID or variable group");
      continue;
    }
    for (Item = 0; Item < AcPlistGetCount(Group); ++Item)
    {
      if (Add)
        AcPlistGetDictionaryItem(Group, Item, &Name, &Value);
      else
      {
        Value = AcPlistGetArrayItem(Group, Item);
        Name = AcPlistGetString(Value);
      }
      if (Name != NULL && Name[0] == '#')
        continue;
      Unicode = NULL;
      ZeroMem(&Encoded, sizeof(Encoded));
      Status = Name == NULL ? EFI_INVALID_PARAMETER : CopyName(Name, &Unicode);
      if (!EFI_ERROR(Status) && Add)
        Status = EncodeValue(Value, &Encoded);
      if (EFI_ERROR(Status))
      {
        if (Unicode != NULL)
          FreePool(Unicode);
        if (Encoded.Data != NULL)
          FreePool(Encoded.Data);
        if (Status == EFI_OUT_OF_RESOURCES)
          return Status;
        InternalConfigurationIssue(Issues, Scope, Index, Name == NULL ? "" : Name,
                                   "invalid variable name or value");
        continue;
      }
      if (Add)
      {
        Nvram->Add[Nvram->AddCount].Guid = Guid;
        Nvram->Add[Nvram->AddCount].Name = Unicode;
        Nvram->Add[Nvram->AddCount++].Value = Encoded;
      }
      else
      {
        Nvram->Delete[Nvram->DeleteCount].Guid = Guid;
        Nvram->Delete[Nvram->DeleteCount++].Name = Unicode;
      }
    }
  }
  return EFI_SUCCESS;
}

EFI_STATUS
InternalConfigurationParseNvram(IN CONST AC_PLIST_NODE *Node, IN OUT AC_NVRAM_CONFIG *Nvram,
                                IN OUT UINTN *IssueCount)
{
  CONST AC_PLIST_NODE *Value;
  CONST CHAR8         *Key;
  EFI_STATUS           Status;

  if (AcPlistGetType(Node) != AcPlistTypeDictionary)
  {
    InternalConfigurationIssue(IssueCount, "", MAX_UINTN, "NVRAM", "expected a dictionary");
    return EFI_SUCCESS;
  }
  for (UINTN Index = 0; Index < AcPlistGetCount(Node); ++Index)
  {
    AcPlistGetDictionaryItem(Node, Index, &Key, &Value);
    if (Key[0] == '#')
      continue;
    if (AsciiStrCmp(Key, "Add") == 0 || AsciiStrCmp(Key, "Delete") == 0)
    {
      Status = ParseEntries(Value, Nvram, AsciiStrCmp(Key, "Add") == 0, IssueCount);
      if (EFI_ERROR(Status))
        return Status;
    }
    else
      InternalConfigurationIssue(IssueCount, "NVRAM.", MAX_UINTN, Key, "unsupported key");
  }
  return EFI_SUCCESS;
}
