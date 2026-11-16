/** @file
  Apple NVRAM V3 value encoding and store validation.
**/

#include "NvramInternal.h"

typedef enum
{
  NvramData,
  NvramBoolean,
  NvramNumber,
  NvramString
} NVRAM_VALUE_TYPE;

STATIC NVRAM_VALUE_TYPE
GetValueType(CONST CHAR8 *Name)
{
  STATIC CONST CHAR8 *CONST Booleans[] = {"auto-boot?",     "default-mac-address?",
                                          "diag-switch?",   "fcode-debug?",
                                          "little-endian?", "ldm",
                                          "oem-banner?",    "oem-logo?",
                                          "real-mode?",     "use-generic?",
                                          "use-nvramrc?",   "test-bool",
                                          "darkboot",       "enter-tdm-mode"};
  STATIC CONST CHAR8 *CONST Numbers[] = {"load-base",
                                         "pci-probe-list",
                                         "pci-probe-mask",
                                         "real-base",
                                         "real-size",
                                         "screen-#columns",
                                         "screen-#rows",
                                         "selftest-#megs",
                                         "virt-base",
                                         "virt-size",
                                         "test-num",
                                         "acc-cm-override-charger-count",
                                         "acc-cm-override-count",
                                         "acc-mb-ld-lifetime"};
  STATIC CONST CHAR8 *CONST Strings[] = {"boot-args",
                                         "boot-command",
                                         "boot-device",
                                         "boot-file",
                                         "boot-screen",
                                         "boot-script",
                                         "console-screen",
                                         "default-client-ip",
                                         "default-gateway-ip",
                                         "default-router-ip",
                                         "default-server-ip",
                                         "default-subnet-mask",
                                         "diag-device",
                                         "diag-file",
                                         "input-device",
                                         "input-device-1",
                                         "mouse-device",
                                         "nvramrc",
                                         "oem-banner",
                                         "oem-logo",
                                         "output-device",
                                         "output-device-1",
                                         "security-mode",
                                         "test-str",
                                         "com.apple.System.boot-nonce"};

  for (UINTN Index = 0; Index < ARRAY_SIZE(Booleans); ++Index)
    if (AsciiStrCmp(Name, Booleans[Index]) == 0)
      return NvramBoolean;
  for (UINTN Index = 0; Index < ARRAY_SIZE(Numbers); ++Index)
    if (AsciiStrCmp(Name, Numbers[Index]) == 0)
      return NvramNumber;
  for (UINTN Index = 0; Index < ARRAY_SIZE(Strings); ++Index)
    if (AsciiStrCmp(Name, Strings[Index]) == 0)
      return NvramString;
  return NvramData;
}

STATIC VOID
EncodeGuid(CONST EFI_GUID *Guid, UINT8 Bytes[16])
{
  Bytes[0] = (UINT8)(Guid->Data1 >> 24);
  Bytes[1] = (UINT8)(Guid->Data1 >> 16);
  Bytes[2] = (UINT8)(Guid->Data1 >> 8);
  Bytes[3] = (UINT8)Guid->Data1;
  Bytes[4] = (UINT8)(Guid->Data2 >> 8);
  Bytes[5] = (UINT8)Guid->Data2;
  Bytes[6] = (UINT8)(Guid->Data3 >> 8);
  Bytes[7] = (UINT8)Guid->Data3;
  CopyMem(Bytes + 8, Guid->Data4, 8);
}

STATIC BOOLEAN
NeedsSystemAlias(CONST AC_APPLE_NVRAM_CONTEXT *Context, CONST AC_NVRAM_VARIABLE *Variable)
{
  STATIC CONST CHAR8 *CONST SystemNames[] = {"allow-root-hash-mismatch",
                                             "auto-boot",
                                             "auto-boot-halt-stage",
                                             "base-system-path",
                                             "boot-args",
                                             "boot-command",
                                             "boot-image",
                                             "bootdelay",
                                             "com.apple.System.boot-nonce",
                                             "darkboot",
                                             "emu",
                                             "one-time-boot-command",
                                             "policy-nonce-digests",
                                             "prevent-restores",
                                             "prev-lang:kbd",
                                             "root-live-fs",
                                             "sep-debug-args",
                                             "StartupMute",
                                             "SystemAudioVolume",
                                             "SystemAudioVolumeExtension",
                                             "SystemAudioVolumeSaved",
                                             "force-lock-bits",
                                             "stress-rack"};
  CONST VOID               *Data;
  UINTN                     Size;

  if (CompareMem(&Variable->Guid, &gAcAppleNvramGuid, sizeof(Variable->Guid)) != 0)
    return FALSE;
  for (UINTN Index = 0; Index < ARRAY_SIZE(SystemNames); ++Index)
    if (AsciiStrCmp(Variable->Name, SystemNames[Index]) == 0)
      return AcAppleNvramGetVariable(Context, &gAcAppleSystemNvramGuid, Variable->Name, &Data,
                                     &Size) == EFI_NOT_FOUND;
  return FALSE;
}

STATIC EFI_STATUS
EncodeValue(CONST AC_NVRAM_VARIABLE *Variable, CHAR8 Text[12], CONST VOID **Data, UINTN *Size)
{
  CONST UINT8 *Bytes = Variable->Data;
  UINT32       Number;

  *Data = Bytes;
  *Size = Variable->Size;
  switch (GetValueType(Variable->Name))
  {
  case NvramBoolean:
    if (*Size != 1 || Bytes[0] > 1)
      return EFI_COMPROMISED_DATA;
    *Data = Bytes[0] != 0 ? "true" : "false";
    *Size = Bytes[0] != 0 ? 4 : 5;
    break;
  case NvramNumber:
    if (*Size != sizeof(Number))
      return EFI_COMPROMISED_DATA;
    Number =
      (UINT32)Bytes[0] | (UINT32)Bytes[1] << 8 | (UINT32)Bytes[2] << 16 | (UINT32)Bytes[3] << 24;
    *Size = AsciiSPrint(Text, 12, "0x%x", Number);
    *Data = Text;
    break;
  case NvramString:
    while (*Size != 0 && Bytes[*Size - 1] == 0)
      --*Size;
    for (UINTN Index = 0; Index < *Size; ++Index)
      if (Bytes[Index] == 0)
        return EFI_COMPROMISED_DATA;
    break;
  case NvramData:
    break;
  }
  return EFI_SUCCESS;
}

STATIC BOOLEAN
ValidName(CONST UINT8 *Name, UINTN Size)
{
  UINT32 Code;
  UINT32 Minimum;
  UINTN  More;

  if (Size < 2 || Name[Size - 1] != 0)
    return FALSE;
  for (UINTN Index = 0; Index + 1 < Size;)
  {
    Code = Name[Index++];
    if (Code == 0)
      return FALSE;
    if (Code < 0x80)
      continue;
    if (Code >= 0xC2 && Code <= 0xDF)
    {
      Code &= 0x1F;
      More = 1;
      Minimum = 0x80;
    }
    else if (Code >= 0xE0 && Code <= 0xEF)
    {
      Code &= 0x0F;
      More = 2;
      Minimum = 0x800;
    }
    else if (Code >= 0xF0 && Code <= 0xF4)
    {
      Code &= 7;
      More = 3;
      Minimum = 0x10000;
    }
    else
      return FALSE;
    if (More > Size - 1 - Index)
      return FALSE;
    while (More-- != 0)
    {
      if ((Name[Index] & 0xC0) != 0x80)
        return FALSE;
      Code = (Code << 6) | (Name[Index++] & 0x3F);
    }
    if (Code < Minimum || Code > 0x10FFFF || (Code >= 0xD800 && Code <= 0xDFFF))
      return FALSE;
  }
  return TRUE;
}

EFI_STATUS
InternalNvramValidateV3(IN CONST VOID *Data, IN UINTN Size)
{
  AC_NVRAM_V3_HEADER   Header;
  AC_NVRAM_V3_VARIABLE Record;
  CONST UINT8         *Bytes = Data;
  UINT8                CommonGuid[16];
  UINT8                SystemGuid[16];
  UINTN                CommonUsed = 0;
  UINTN                SystemUsed = 0;
  UINTN                Offset = sizeof(Header);
  UINTN                Count = 0;
  UINTN                Length;
  CONST UINT8         *Value;

  if (Data == NULL || Size < sizeof(Header) + sizeof(Record) || Size > AC_NVRAM_MAX_STORE)
    return EFI_COMPROMISED_DATA;
  CopyMem(&Header, Data, sizeof(Header));
  if (Header.Signature != AC_NVRAM_V3_SIGNATURE || Header.Version != AC_NVRAM_V3_VERSION ||
      Header.Size != Size || Header.CommonSize < sizeof(Header) || Header.CommonSize > Size ||
      Header.SystemSize == 0 || Header.SystemSize != Size - Header.CommonSize)
    return EFI_COMPROMISED_DATA;
  EncodeGuid(&gAcAppleNvramGuid, CommonGuid);
  EncodeGuid(&gAcAppleSystemNvramGuid, SystemGuid);
  while (Offset < Size)
  {
    if (Bytes[Offset] == 0xFF)
    {
      for (; Offset < Size; ++Offset)
        if (Bytes[Offset] != 0xFF)
          return EFI_COMPROMISED_DATA;
      break;
    }
    if (Size - Offset < sizeof(Record) || ++Count > AC_NVRAM_MAX_VARIABLES)
      return EFI_COMPROMISED_DATA;
    CopyMem(&Record, Bytes + Offset, sizeof(Record));
    Length = Size - Offset - sizeof(Record);
    if (Record.Signature != AC_NVRAM_V3_VARIABLE_SIGNATURE || Record.State != AC_NVRAM_V3_ADDED ||
        Record.NameSize > Length || Record.DataSize > Length - Record.NameSize ||
        !ValidName(Bytes + Offset + sizeof(Record), Record.NameSize))
      return EFI_COMPROMISED_DATA;
    Value = Bytes + Offset + sizeof(Record) + Record.NameSize;
    if (Record.Crc != CalculateCrc32((VOID *)Value, Record.DataSize))
      return EFI_COMPROMISED_DATA;
    Length = sizeof(Record) + Record.NameSize + Record.DataSize;
    if (CompareMem(Record.Guid, SystemGuid, sizeof(SystemGuid)) == 0)
      SystemUsed += Length;
    else if (CompareMem(Record.Guid, CommonGuid, sizeof(CommonGuid)) == 0)
      CommonUsed += Length;
    else
      return EFI_COMPROMISED_DATA;
    Offset += Length;
  }
  return CommonUsed <= Header.CommonSize - sizeof(Header) && SystemUsed <= Header.SystemSize
           ? EFI_SUCCESS
           : EFI_COMPROMISED_DATA;
}

EFI_STATUS
InternalNvramEncodeV3(IN OUT AC_APPLE_NVRAM_CONTEXT *Context)
{
  AC_NVRAM_V3_HEADER   Header = {0};
  AC_NVRAM_V3_VARIABLE Record;
  AC_NVRAM_VARIABLE   *Variable;
  CONST VOID          *Data;
  UINTN                Size;
  UINTN                Length;
  UINTN                CommonUsed = 0;
  UINTN                SystemUsed = 0;
  UINTN                Offset = sizeof(Header);
  UINTN                Total;
  UINTN                RecordCount = 0;
  UINT8               *Buffer;
  CHAR8                Text[12];
  EFI_STATUS           Status;

  for (Variable = Context->Variables; Variable != NULL; Variable = Variable->Next)
  {
    Status = EncodeValue(Variable, Text, &Data, &Size);
    if (EFI_ERROR(Status))
    {
      DEBUG((DEBUG_ERROR, "ACNV: Encode %g:%a: %r\n", &Variable->Guid, Variable->Name, Status));
      return Status;
    }
    Length = sizeof(Record) + AsciiStrLen(Variable->Name) + 1 + Size;
    RecordCount += NeedsSystemAlias(Context, Variable) ? 2 : 1;
    if (RecordCount > AC_NVRAM_MAX_VARIABLES)
      return EFI_BAD_BUFFER_SIZE;
    if (NeedsSystemAlias(Context, Variable))
    {
      if (Length > AC_NVRAM_MAX_STORE - CommonUsed - SystemUsed)
        return EFI_BAD_BUFFER_SIZE;
      SystemUsed += Length;
    }
    if (Length > AC_NVRAM_MAX_STORE - CommonUsed - SystemUsed)
      return EFI_BAD_BUFFER_SIZE;
    if (CompareMem(&Variable->Guid, &gAcAppleSystemNvramGuid, sizeof(Variable->Guid)) == 0)
      SystemUsed += Length;
    else
      CommonUsed += Length;
  }
  // Both namespaces retain room for variables created after the initial import.
  Total = ALIGN_VALUE(sizeof(Header) + CommonUsed + MAX(CommonUsed, SIZE_4KB), SIZE_4KB);
  Header.CommonSize = (UINT32)Total;
  Total = ALIGN_VALUE(SystemUsed + MAX(SystemUsed, SIZE_4KB), SIZE_4KB);
  Header.SystemSize = (UINT32)Total;
  Total += Header.CommonSize;
  if (Total > AC_NVRAM_MAX_STORE)
    return EFI_BAD_BUFFER_SIZE;
  Buffer = AllocateZeroPool(Total);
  if (Buffer == NULL)
    return EFI_OUT_OF_RESOURCES;
  SetMem(Buffer, Total, 0xFF);
  Header.Signature = AC_NVRAM_V3_SIGNATURE;
  Header.Size = (UINT32)Total;
  Header.Version = AC_NVRAM_V3_VERSION;
  CopyMem(Buffer, &Header, sizeof(Header));
  for (Variable = Context->Variables; Variable != NULL; Variable = Variable->Next)
  {
    Status = EncodeValue(Variable, Text, &Data, &Size);
    if (EFI_ERROR(Status))
      goto Failure;
    ZeroMem(&Record, sizeof(Record));
    Record.Signature = AC_NVRAM_V3_VARIABLE_SIGNATURE;
    Record.State = AC_NVRAM_V3_ADDED;
    Record.Attributes = Variable->Attributes;
    Record.NameSize = (UINT32)AsciiStrLen(Variable->Name) + 1;
    Record.DataSize = (UINT32)Size;
    Record.Crc = Size == 0 ? 0 : CalculateCrc32((VOID *)Data, Size);
    for (UINTN Copy = 0; Copy < (NeedsSystemAlias(Context, Variable) ? 2U : 1U); ++Copy)
    {
      // ARM NVRAM redirects allowlisted names to System; retain the original too.
      EncodeGuid(Copy == 0 ? &Variable->Guid : &gAcAppleSystemNvramGuid, Record.Guid);
      CopyMem(Buffer + Offset, &Record, sizeof(Record));
      Offset += sizeof(Record);
      CopyMem(Buffer + Offset, Variable->Name, Record.NameSize);
      Offset += Record.NameSize;
      if (Size != 0)
        CopyMem(Buffer + Offset, Data, Size);
      Offset += Size;
    }
  }
  Status = InternalNvramValidateV3(Buffer, Total);
  if (EFI_ERROR(Status))
    goto Failure;
  Context->Info.Data = Buffer;
  Context->Info.Size = Total;
  Context->Info.BankCount = 1;
  Context->Info.CurrentBank = 0;
  Context->Info.RawController = 0;
  return EFI_SUCCESS;

Failure:
  ZeroMem(Buffer, Total);
  FreePool(Buffer);
  return Status;
}
