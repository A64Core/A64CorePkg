/** @file
  Apple Device Tree configuration mapping and ownership.
**/

#include "ConfigurationInternal.h"

STATIC BOOLEAN
ValidPath(CONST CHAR8 *Path)
{
  CONST CHAR8 *Start;
  UINTN        Length;
  UINTN        Depth = 1;

  if (Path == NULL || *Path++ != '/')
    return FALSE;
  while (*Path != '\0')
  {
    Start = Path;
    Length = 0;
    while (*Path != '\0' && *Path != '/')
    {
      if ((UINT8)*Path < 0x21 || (UINT8)*Path > 0x7E || ++Length > 63)
        return FALSE;
      ++Path;
    }
    if (Length == 0 || (Length == 1 && Start[0] == '.') ||
        (Length == 2 && Start[0] == '.' && Start[1] == '.') || ++Depth > 64)
      return FALSE;
    if (*Path == '/' && *++Path == '\0')
      return FALSE;
  }
  return TRUE;
}

STATIC BOOLEAN
ValidProperty(CONST CHAR8 *Name)
{
  UINTN Length;

  if (Name == NULL || Name[0] == '\0' || AsciiStrCmp(Name, "name") == 0)
    return FALSE;
  for (Length = 0; Name[Length] != '\0'; ++Length)
    if (Length >= 31 || (UINT8)Name[Length] < 0x21 || (UINT8)Name[Length] > 0x7E ||
        Name[Length] == '/')
      return FALSE;
  return TRUE;
}

STATIC EFI_STATUS
CopyText(CONST CHAR8 *Text, CHAR8 **Copy)
{
  UINTN Size = AsciiStrLen(Text) + 1;

  *Copy = AllocateZeroPool(Size);
  if (*Copy == NULL)
    return EFI_OUT_OF_RESOURCES;
  CopyMem(*Copy, Text, Size);
  return EFI_SUCCESS;
}

STATIC VOID
AdtIssue(AC_ADT_CONFIG *Adt, UINTN *Issues, CONST CHAR8 *Scope, UINTN Index, CONST CHAR8 *Field,
         CONST CHAR8 *Reason)
{
  Adt->Invalid = TRUE;
  InternalConfigurationIssue(Issues, Scope, Index, Field, Reason);
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
InternalConfigurationFreeAdt(IN OUT AC_ADT_CONFIG *Adt)
{
  for (UINTN Index = 0; Index < Adt->AddCount; ++Index)
  {
    AC_ADT_ADD_ENTRY *Entry = &Adt->Add[Index];

    for (UINTN Property = 0; Property < Entry->PropertyCount; ++Property)
    {
      if (Entry->Properties[Property].Name != NULL)
        FreePool(Entry->Properties[Property].Name);
      if (Entry->Properties[Property].Value.Data != NULL)
        FreePool(Entry->Properties[Property].Value.Data);
    }
    if (Entry->Properties != NULL)
      FreePool(Entry->Properties);
    if (Entry->Path != NULL)
      FreePool(Entry->Path);
  }
  for (UINTN Index = 0; Index < Adt->DeleteCount; ++Index)
  {
    if (Adt->Delete[Index].Path != NULL)
      FreePool(Adt->Delete[Index].Path);
    if (Adt->Delete[Index].Property != NULL)
      FreePool(Adt->Delete[Index].Property);
  }
  if (Adt->Add != NULL)
    FreePool(Adt->Add);
  if (Adt->Delete != NULL)
    FreePool(Adt->Delete);
  ZeroMem(Adt, sizeof(*Adt));
}

STATIC EFI_STATUS
ParseAdd(CONST AC_PLIST_NODE *Node, AC_ADT_CONFIG *Adt, UINTN *Issues)
{
  CONST AC_PLIST_NODE   *Group;
  CONST AC_PLIST_NODE   *Value;
  CONST CHAR8           *Path;
  CONST CHAR8           *Name;
  AC_ADT_ADD_ENTRY      *Entry;
  AC_ADT_PROPERTY_ENTRY *Property;
  UINTN                  Count = AcPlistGetCount(Node);
  EFI_STATUS             Status;

  if (AcPlistGetType(Node) != AcPlistTypeDictionary)
  {
    AdtIssue(Adt, Issues, "ADT.", MAX_UINTN, "Add", "expected a dictionary");
    return EFI_SUCCESS;
  }
  if (Count > MAX_UINTN / sizeof(*Adt->Add))
    return EFI_OUT_OF_RESOURCES;
  if (Count != 0)
  {
    Adt->Add = AllocateZeroPool(Count * sizeof(*Adt->Add));
    if (Adt->Add == NULL)
      return EFI_OUT_OF_RESOURCES;
  }
  for (UINTN Index = 0; Index < Count; ++Index)
  {
    AcPlistGetDictionaryItem(Node, Index, &Path, &Group);
    if (Path[0] == '#')
      continue;
    if (!ValidPath(Path) || AcPlistGetType(Group) != AcPlistTypeDictionary)
    {
      AdtIssue(Adt, Issues, "ADT.Add", Index, Path, "invalid path or property dictionary");
      continue;
    }
    Entry = &Adt->Add[Adt->AddCount++];
    Status = CopyText(Path, &Entry->Path);
    if (EFI_ERROR(Status))
      return Status;
    if (AcPlistGetCount(Group) > MAX_UINTN / sizeof(*Entry->Properties))
      return EFI_OUT_OF_RESOURCES;
    if (AcPlistGetCount(Group) != 0)
    {
      Entry->Properties = AllocateZeroPool(AcPlistGetCount(Group) * sizeof(*Entry->Properties));
      if (Entry->Properties == NULL)
        return EFI_OUT_OF_RESOURCES;
    }
    for (UINTN Item = 0; Item < AcPlistGetCount(Group); ++Item)
    {
      AcPlistGetDictionaryItem(Group, Item, &Name, &Value);
      if (!ValidProperty(Name))
      {
        AdtIssue(Adt, Issues, "ADT.Add", Index, Name, "invalid or protected property name");
        continue;
      }
      Property = &Entry->Properties[Entry->PropertyCount++];
      Status = CopyText(Name, &Property->Name);
      if (!EFI_ERROR(Status))
        Status = EncodeValue(Value, &Property->Value);
      if (Status == EFI_OUT_OF_RESOURCES)
        return Status;
      if (EFI_ERROR(Status))
        AdtIssue(Adt, Issues, "ADT.Add", Index, Name, "invalid property value");
    }
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ParseDelete(CONST AC_PLIST_NODE *Node, AC_ADT_CONFIG *Adt, UINTN *Issues)
{
  CONST AC_PLIST_NODE *Item;
  CONST AC_PLIST_NODE *Value;
  CONST CHAR8         *Key;
  CONST CHAR8         *Path;
  CONST CHAR8         *Property;
  AC_ADT_DELETE_ENTRY *Entry;
  UINTN                Count = AcPlistGetCount(Node);
  BOOLEAN              Valid;
  EFI_STATUS           Status;

  if (AcPlistGetType(Node) != AcPlistTypeArray)
  {
    AdtIssue(Adt, Issues, "ADT.", MAX_UINTN, "Delete", "expected an array");
    return EFI_SUCCESS;
  }
  if (Count > MAX_UINTN / sizeof(*Adt->Delete))
    return EFI_OUT_OF_RESOURCES;
  if (Count != 0)
  {
    Adt->Delete = AllocateZeroPool(Count * sizeof(*Adt->Delete));
    if (Adt->Delete == NULL)
      return EFI_OUT_OF_RESOURCES;
  }
  for (UINTN Index = 0; Index < Count; ++Index)
  {
    Item = AcPlistGetArrayItem(Node, Index);
    Path = NULL;
    Property = NULL;
    Valid = TRUE;
    if (AcPlistGetType(Item) != AcPlistTypeDictionary)
      Valid = FALSE;
    for (UINTN Field = 0; Field < AcPlistGetCount(Item); ++Field)
    {
      if (AcPlistGetType(Item) != AcPlistTypeDictionary)
        break;
      AcPlistGetDictionaryItem(Item, Field, &Key, &Value);
      if (Key[0] == '#')
        continue;
      if (AsciiStrCmp(Key, "Path") == 0)
        Path = AcPlistGetString(Value);
      else if (AsciiStrCmp(Key, "Property") == 0)
      {
        Property = AcPlistGetString(Value);
        if (!ValidProperty(Property))
          Valid = FALSE;
      }
      else
        Valid = FALSE;
    }
    if (!Valid || !ValidPath(Path) || (Property == NULL && AsciiStrCmp(Path, "/") == 0))
    {
      AdtIssue(Adt, Issues, "ADT.Delete", Index, "", "invalid path, property, or entry");
      continue;
    }
    Entry = &Adt->Delete[Adt->DeleteCount++];
    Status = CopyText(Path, &Entry->Path);
    if (!EFI_ERROR(Status) && Property != NULL)
      Status = CopyText(Property, &Entry->Property);
    if (EFI_ERROR(Status))
      return Status;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
InternalConfigurationParseAdt(IN CONST AC_PLIST_NODE *Node, IN OUT AC_ADT_CONFIG *Adt,
                              IN OUT UINTN *IssueCount)
{
  CONST AC_PLIST_NODE *Value;
  CONST CHAR8         *Key;
  EFI_STATUS           Status;

  if (AcPlistGetType(Node) != AcPlistTypeDictionary)
  {
    AdtIssue(Adt, IssueCount, "", MAX_UINTN, "ADT", "expected a dictionary");
    return EFI_SUCCESS;
  }
  for (UINTN Index = 0; Index < AcPlistGetCount(Node); ++Index)
  {
    AcPlistGetDictionaryItem(Node, Index, &Key, &Value);
    if (Key[0] == '#')
      continue;
    if (AsciiStrCmp(Key, "Add") == 0)
      Status = ParseAdd(Value, Adt, IssueCount);
    else if (AsciiStrCmp(Key, "Delete") == 0)
      Status = ParseDelete(Value, Adt, IssueCount);
    else
    {
      AdtIssue(Adt, IssueCount, "ADT.", MAX_UINTN, Key, "unsupported key");
      continue;
    }
    if (EFI_ERROR(Status))
      return Status;
  }
  return EFI_SUCCESS;
}
