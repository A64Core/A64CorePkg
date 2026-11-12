/** @file
  Configuration initialization and release.
**/

#include "ConfigurationInternal.h"

VOID
InternalConfigurationIssue(IN OUT UINTN *IssueCount, IN CONST CHAR8 *Scope, IN UINTN Index,
                           IN CONST CHAR8 *Field, IN CONST CHAR8 *Reason)
{
  ++*IssueCount;
  if (Index == MAX_UINTN)
    DEBUG((DEBUG_ERROR, "AC: Config %a%a: %a\n", Scope, Field, Reason));
  else
    DEBUG((DEBUG_ERROR, "AC: Config %a[%u]%a%a: %a\n", Scope, (UINT32)Index,
           Field[0] == '\0' ? "" : ".", Field, Reason));
}

VOID
InternalConfigurationFreeDriver(IN OUT AC_UEFI_DRIVER_ENTRY *Driver)
{
  if (Driver->Path != NULL)
    FreePool(Driver->Path);
  if (Driver->Arguments != NULL)
    FreePool(Driver->Arguments);
  if (Driver->Comment != NULL)
    FreePool(Driver->Comment);
  ZeroMem(Driver, sizeof(*Driver));
}

VOID
AcConfigurationFree(IN OUT AC_GLOBAL_CONFIG *Config OPTIONAL)
{
  UINTN Index;

  if (Config == NULL)
    return;
  for (Index = 0; Index < Config->Uefi.DriverCount; ++Index)
    InternalConfigurationFreeDriver(&Config->Uefi.Drivers[Index]);
  if (Config->Uefi.Drivers != NULL)
    FreePool(Config->Uefi.Drivers);
  for (Index = 0; Index < Config->Kernel.PatchCount; ++Index)
    InternalConfigurationFreePatch(&Config->Kernel.Patches[Index]);
  if (Config->Kernel.Patches != NULL)
    FreePool(Config->Kernel.Patches);
  ZeroMem(Config, sizeof(*Config));
}

EFI_STATUS
AcConfigurationInit(IN CONST VOID *Buffer, IN UINTN BufferSize, OUT AC_GLOBAL_CONFIG *Config,
                    OUT UINTN *IssueCount OPTIONAL)
{
  EFI_STATUS           Status;
  AC_PLIST_DOCUMENT   *Document;
  CONST AC_PLIST_NODE *Root;
  CONST AC_PLIST_NODE *Value;
  CONST CHAR8         *Key;
  UINTN                Index;
  UINTN                Issues;

  if (IssueCount != NULL)
    *IssueCount = 0;
  if (Config == NULL)
    return EFI_INVALID_PARAMETER;
  ZeroMem(Config, sizeof(*Config));
  Status = AcPlistParse(Buffer, BufferSize, &Document);
  if (EFI_ERROR(Status))
    return Status;
  Issues = 0;
  Root = AcPlistGetRoot(Document);
  if (AcPlistGetType(Root) != AcPlistTypeDictionary)
  {
    InternalConfigurationIssue(&Issues, "root", MAX_UINTN, "", "expected a dictionary");
    Status = EFI_COMPROMISED_DATA;
  }
  else
  {
    for (Index = 0; Index < AcPlistGetCount(Root); ++Index)
    {
      AcPlistGetDictionaryItem(Root, Index, &Key, &Value);
      if (Key[0] == '#')
        continue;
      if (AsciiStrCmp(Key, "UEFI") == 0)
        Status = InternalConfigurationParseUefi(Value, &Config->Uefi, &Issues);
      else if (AsciiStrCmp(Key, "Kernel") == 0)
        Status = InternalConfigurationParseKernel(Value, &Config->Kernel, &Issues);
      else
        InternalConfigurationIssue(&Issues, "", MAX_UINTN, Key, "unsupported key");
      if (EFI_ERROR(Status))
        break;
    }
  }
  AcPlistFree(Document);
  if (IssueCount != NULL)
    *IssueCount = Issues;
  if (EFI_ERROR(Status))
    AcConfigurationFree(Config);
  return Status;
}
