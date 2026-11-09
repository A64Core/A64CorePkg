/** @file
  UEFI driver configuration mapping.
**/

#include "ConfigurationInternal.h"

STATIC BOOLEAN
ValidDriverPath(CONST CHAR8 *Path)
{
  CONST CHAR8 *Part;
  CONST CHAR8 *End;
  UINTN        Length;
  UINT8        Byte;

  if ((Path == NULL) || (Path[0] == '\0'))
    return FALSE;
  Part = Path;
  for (End = Path;; ++End)
  {
    Byte = (UINT8)*End;
    if ((Byte == ':') || ((Byte != 0) && (Byte < 0x20)) || (Byte == 0x7F) ||
        ((Byte == 0xC2) && ((UINT8)End[1] >= 0x80) && ((UINT8)End[1] <= 0x9F)))
      return FALSE;
    if ((Byte != 0) && (Byte != '/') && (Byte != '\\'))
      continue;
    Length = (UINTN)(End - Part);
    if ((Length == 0) || ((Length == 1) && (Part[0] == '.')) ||
        ((Length == 2) && (Part[0] == '.') && (Part[1] == '.')))
      return FALSE;
    if (Byte == 0)
      return TRUE;
    Part = End + 1;
  }
}

STATIC EFI_STATUS
ParseDriver(CONST AC_PLIST_NODE *Node, UINTN Index, AC_UEFI_DRIVER_ENTRY *Driver, UINTN *IssueCount)
{
  CONST AC_PLIST_NODE *Value;
  CONST CHAR8         *Key;
  CONST CHAR8         *Text;
  CHAR8              **StringField;
  BOOLEAN             *BooleanField;
  UINTN                FieldIndex;
  UINTN                Length;

  for (FieldIndex = 0; FieldIndex < AcPlistGetCount(Node); ++FieldIndex)
  {
    AcPlistGetDictionaryItem(Node, FieldIndex, &Key, &Value);
    if (Key[0] == '#')
      continue;
    StringField = NULL;
    BooleanField = NULL;
    if (AsciiStrCmp(Key, "Path") == 0)
      StringField = &Driver->Path;
    else if (AsciiStrCmp(Key, "Arguments") == 0)
      StringField = &Driver->Arguments;
    else if (AsciiStrCmp(Key, "Comment") == 0)
      StringField = &Driver->Comment;
    else if (AsciiStrCmp(Key, "Enabled") == 0)
      BooleanField = &Driver->Enabled;
    else if (AsciiStrCmp(Key, "LoadEarly") == 0)
      BooleanField = &Driver->LoadEarly;
    else
    {
      InternalConfigurationIssue(IssueCount, "UEFI.Drivers", Index, Key, "unsupported key");
      continue;
    }
    if (StringField != NULL)
    {
      Text = AcPlistGetString(Value);
      if (Text == NULL)
      {
        InternalConfigurationIssue(IssueCount, "UEFI.Drivers", Index, Key, "expected a string");
        continue;
      }
      Length = AsciiStrLen(Text);
      if (Length != 0)
      {
        *StringField = AllocateZeroPool(Length + 1);
        if (*StringField == NULL)
          return EFI_OUT_OF_RESOURCES;
        CopyMem(*StringField, Text, Length);
      }
    }
    else if (EFI_ERROR(AcPlistGetBoolean(Value, BooleanField)))
      InternalConfigurationIssue(IssueCount, "UEFI.Drivers", Index, Key, "expected a boolean");
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ParseDrivers(CONST AC_PLIST_NODE *Node, AC_UEFI_CONFIG *Uefi, UINTN *IssueCount)
{
  CONST AC_PLIST_NODE  *Value;
  AC_UEFI_DRIVER_ENTRY *Driver;
  EFI_STATUS            Status;
  UINTN                 Count;
  UINTN                 Index;

  if (AcPlistGetType(Node) != AcPlistTypeArray)
  {
    InternalConfigurationIssue(IssueCount, "UEFI.", MAX_UINTN, "Drivers", "expected an array");
    return EFI_SUCCESS;
  }
  Count = AcPlistGetCount(Node);
  if (Count == 0)
    return EFI_SUCCESS;
  if (Count > MAX_UINTN / sizeof(*Uefi->Drivers))
    return EFI_OUT_OF_RESOURCES;
  Uefi->Drivers = AllocateZeroPool(Count * sizeof(*Uefi->Drivers));
  if (Uefi->Drivers == NULL)
    return EFI_OUT_OF_RESOURCES;
  for (Index = 0; Index < Count; ++Index)
  {
    Value = AcPlistGetArrayItem(Node, Index);
    if (AcPlistGetType(Value) != AcPlistTypeDictionary)
    {
      InternalConfigurationIssue(IssueCount, "UEFI.Drivers", Index, "", "expected a dictionary");
      continue;
    }
    Driver = &Uefi->Drivers[Uefi->DriverCount++];
    Status = ParseDriver(Value, Index, Driver, IssueCount);
    if (EFI_ERROR(Status))
      return Status;
    if (Driver->Enabled && !ValidDriverPath(Driver->Path))
    {
      InternalConfigurationIssue(IssueCount, "UEFI.Drivers", Index, "Path", "invalid driver path");
      InternalConfigurationFreeDriver(Driver);
      --Uefi->DriverCount;
    }
  }
  if (Uefi->DriverCount == 0)
  {
    FreePool(Uefi->Drivers);
    Uefi->Drivers = NULL;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
InternalConfigurationParseUefi(IN CONST AC_PLIST_NODE *Node, IN OUT AC_UEFI_CONFIG *Uefi,
                               IN OUT UINTN *IssueCount)
{
  CONST AC_PLIST_NODE *Value;
  CONST CHAR8         *Key;
  EFI_STATUS           Status;
  UINTN                Index;

  if (AcPlistGetType(Node) != AcPlistTypeDictionary)
  {
    InternalConfigurationIssue(IssueCount, "", MAX_UINTN, "UEFI", "expected a dictionary");
    return EFI_SUCCESS;
  }
  for (Index = 0; Index < AcPlistGetCount(Node); ++Index)
  {
    AcPlistGetDictionaryItem(Node, Index, &Key, &Value);
    if (Key[0] == '#')
      continue;
    if (AsciiStrCmp(Key, "Drivers") == 0)
    {
      Status = ParseDrivers(Value, Uefi, IssueCount);
      if (EFI_ERROR(Status))
        return Status;
    }
    else
      InternalConfigurationIssue(IssueCount, "UEFI.", MAX_UINTN, Key, "unsupported key");
  }
  return EFI_SUCCESS;
}
