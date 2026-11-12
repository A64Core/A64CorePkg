/** @file
  Active Preboot directory selection and kernel resource location.
**/

#include "BootManagementInternal.h"

STATIC INTN
HexDigit(CHAR16 Character)
{
  if (Character >= L'0' && Character <= L'9')
    return Character - L'0';
  if (Character >= L'a' && Character <= L'f')
    return Character - L'a' + 10;
  if (Character >= L'A' && Character <= L'F')
    return Character - L'A' + 10;
  return -1;
}

BOOLEAN
InternalParseBootUuid(CONST CHAR16 *Text, OUT UINT8 Uuid[16])
{
  UINTN Index;
  UINTN Digit = 0;
  INTN  Value;

  if (StrLen(Text) != 36)
    return FALSE;
  ZeroMem(Uuid, 16);
  for (Index = 0; Index < 36; ++Index)
  {
    if (Index == 8 || Index == 13 || Index == 18 || Index == 23)
    {
      if (Text[Index] != L'-')
        return FALSE;
      continue;
    }
    Value = HexDigit(Text[Index]);
    if (Value < 0)
      return FALSE;
    Uuid[Digit / 2] |= (UINT8)(Value << ((Digit & 1) == 0 ? 4 : 0));
    ++Digit;
  }
  return TRUE;
}

VOID
AcFreeBootResources(IN OUT AC_BOOT_RESOURCES *Resources OPTIONAL)
{
  if (Resources == NULL)
    return;
  if (Resources->BootDirectory != NULL)
    FreePool(Resources->BootDirectory);
  if (Resources->KernelPath != NULL)
    FreePool(Resources->KernelPath);
  if (Resources->TicketPath != NULL)
    FreePool(Resources->TicketPath);
  if (Resources->BaseSystemPath != NULL)
    FreePool(Resources->BaseSystemPath);
  if (Resources->BaseSystemTrustCachePath != NULL)
    FreePool(Resources->BaseSystemTrustCachePath);
  if (Resources->StaticTrustCachePath != NULL)
    FreePool(Resources->StaticTrustCachePath);
  if (Resources->BaseSystemRootHashPath != NULL)
    FreePool(Resources->BaseSystemRootHashPath);
  if (Resources->RootHashPath != NULL)
    FreePool(Resources->RootHashPath);
  if (Resources->IscDevicePath != NULL)
    FreePool(Resources->IscDevicePath);
  if (Resources->IscDirectoryPath != NULL)
    FreePool(Resources->IscDirectoryPath);
  if (Resources->ProductVersion != NULL)
    FreePool(Resources->ProductVersion);
  if (Resources->ProductBuildVersion != NULL)
    FreePool(Resources->ProductBuildVersion);
  if (Resources->RestoreVersion != NULL)
    FreePool(Resources->RestoreVersion);
  ZeroMem(Resources, sizeof(*Resources));
}

EFI_STATUS
InternalResolveBootResources(EFI_FILE_PROTOCOL *Root, CONST CHAR16 *DirectoryPath,
                             OUT AC_BOOT_RESOURCES *Resources)
{
  CHAR16            *SelectorPath;
  CHAR16             Hash[97];
  UINT8             *Selector = NULL;
  UINT8              Uuid[16];
  UINTN              Size;
  UINTN              Index;
  EFI_FILE_PROTOCOL *Kernel;
  EFI_FILE_INFO     *Info = NULL;
  EFI_STATUS         Status;

  ZeroMem(Resources, sizeof(*Resources));
  if (DirectoryPath == NULL || DirectoryPath[0] != L'\\' ||
      !InternalParseBootUuid(DirectoryPath + 1, Uuid))
    return EFI_INVALID_PARAMETER;
  SelectorPath = InternalJoinBootPath(DirectoryPath, L"boot\\active");
  if (SelectorPath == NULL)
    return EFI_OUT_OF_RESOURCES;
  Status = InternalReadBootFile(Root, SelectorPath, 98, (VOID **)&Selector, &Size);
  FreePool(SelectorPath);
  if (EFI_ERROR(Status))
    return Status;
  if (Size != 96 && !(Size == 97 && Selector[96] == '\n') &&
      !(Size == 98 && Selector[96] == '\r' && Selector[97] == '\n'))
  {
    Status = EFI_VOLUME_CORRUPTED;
    goto Done;
  }
  for (Index = 0; Index < 96; ++Index)
  {
    if (HexDigit(Selector[Index]) < 0)
    {
      Status = EFI_VOLUME_CORRUPTED;
      goto Done;
    }
    Hash[Index] = Selector[Index];
  }
  Hash[96] = L'\0';
  SelectorPath = InternalJoinBootPath(DirectoryPath, L"boot");
  if (SelectorPath == NULL)
  {
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }
  Resources->BootDirectory = InternalJoinBootPath(SelectorPath, Hash);
  FreePool(SelectorPath);
  if (Resources->BootDirectory == NULL)
  {
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }
  Resources->KernelPath = InternalJoinBootPath(
    Resources->BootDirectory, L"System\\Library\\Caches\\com.apple.kernelcaches\\kernelcache");
  if (Resources->KernelPath == NULL)
  {
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }
  Status = Root->Open(Root, &Kernel, Resources->KernelPath, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(Status))
    goto Done;
  Status = InternalGetBootFileInfo(Kernel, &Info);
  if (!EFI_ERROR(Status) && ((Info->Attribute & EFI_FILE_DIRECTORY) != 0 || Info->FileSize == 0))
    Status = EFI_VOLUME_CORRUPTED;
  Kernel->Close(Kernel);

Done:
  if (Info != NULL)
    FreePool(Info);
  FreePool(Selector);
  if (EFI_ERROR(Status))
    AcFreeBootResources(Resources);
  return Status;
}

EFI_STATUS
AcResolveBootResources(IN CONST AC_BOOT_ENTRY *Entry, OUT AC_BOOT_RESOURCES *Resources)
{
  EFI_FILE_PROTOCOL *Root;
  EFI_HANDLE         Handle;
  EFI_STATUS         Status;

  if (Resources == NULL)
    return EFI_INVALID_PARAMETER;
  ZeroMem(Resources, sizeof(*Resources));
  if (Entry == NULL || Entry->DirectoryPath == NULL || Entry->DevicePath == NULL)
    return EFI_INVALID_PARAMETER;
  Status = InternalOpenBootRoot(Entry, &Root, &Handle);
  if (EFI_ERROR(Status))
    return Status;
  if (Entry->Type == AcBootEntryRecovery)
    Status = InternalResolveRecoveryResources(Handle, Root, Entry->DirectoryPath, Resources);
  else
    Status = InternalResolveBootResources(Root, Entry->DirectoryPath, Resources);
  Root->Close(Root);
  return Status;
}
