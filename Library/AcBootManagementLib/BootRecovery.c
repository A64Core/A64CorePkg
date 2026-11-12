/** @file
  Independent Recovery resource discovery and same-disk iSC association.
**/

#include "BootManagementInternal.h"

#define AC_RECOVERY_BOOT_DIRECTORY L"\\3D3287DE-280D-4619-AAAB-D97469CA9C71\\boot"
#define AC_BOOT_VERSION_LIMIT (1024U * 1024U)

CONST EFI_GUID gAcBootApfsIscPartitionType = {
  0x69646961, 0x6700, 0x11AA, {0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC}};
CONST EFI_GUID gAcBootApfsRecoveryPartitionType = {
  0x52637672, 0x7900, 0x11AA, {0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC}};

STATIC BOOLEAN
ParseHash(CONST CHAR16 *Text, OUT UINT8 Hash[48])
{
  UINTN  Index;
  UINT8  Value;
  CHAR16 Character;

  if (StrLen(Text) != 96)
    return FALSE;
  ZeroMem(Hash, 48);
  for (Index = 0; Index < 96; ++Index)
  {
    Character = Text[Index];
    if (Character >= L'0' && Character <= L'9')
      Value = (UINT8)(Character - L'0');
    else if (Character >= L'A' && Character <= L'F')
      Value = (UINT8)(Character - L'A' + 10);
    else if (Character >= L'a' && Character <= L'f')
      Value = (UINT8)(Character - L'a' + 10);
    else
      return FALSE;
    Hash[Index / 2] |= (UINT8)(Value << ((Index & 1) == 0 ? 4 : 0));
  }
  return TRUE;
}

STATIC EFI_STATUS
LocateFile(EFI_FILE_PROTOCOL *Root, CONST CHAR16 *Directory, CONST CHAR16 *Name, OUT CHAR16 **Path)
{
  EFI_FILE_PROTOCOL *File;
  EFI_FILE_INFO     *Info = NULL;
  EFI_STATUS         Status;

  *Path = InternalJoinBootPath(Directory, Name);
  if (*Path == NULL)
    return EFI_OUT_OF_RESOURCES;
  Status = Root->Open(Root, &File, *Path, EFI_FILE_MODE_READ, 0);
  if (!EFI_ERROR(Status))
  {
    Status = InternalGetBootFileInfo(File, &Info);
    if (!EFI_ERROR(Status) && ((Info->Attribute & EFI_FILE_DIRECTORY) != 0 || Info->FileSize == 0))
      Status = EFI_VOLUME_CORRUPTED;
    if (Info != NULL)
      FreePool(Info);
    File->Close(File);
  }
  if (EFI_ERROR(Status))
    DEBUG((DEBUG_WARN, "ACBM: Recovery resource %s: %r\n", *Path, Status));
  return Status;
}

STATIC EFI_STATUS
ReadTicket(EFI_FILE_PROTOCOL *Root, CONST CHAR16 *Path, OUT VOID **Buffer, OUT UINTN *Size,
           OUT UINT8 Hash[48])
{
  AC_IMG4_MANIFEST Manifest;
  EFI_STATUS       Status;

  Status = InternalReadBootFile(Root, Path, AC_BOOT_TICKET_LIMIT, Buffer, Size);
  if (EFI_ERROR(Status))
    return Status;
  Status = AcImg4GetManifest(*Buffer, *Size, &Manifest);
  if (Status == EFI_NOT_FOUND)
    Status = EFI_COMPROMISED_DATA;
  if (!EFI_ERROR(Status) && (Manifest.Data != *Buffer || Manifest.Size != *Size))
    Status = EFI_COMPROMISED_DATA;
  if (!EFI_ERROR(Status) && !Sha384HashAll(*Buffer, *Size, Hash))
    Status = EFI_DEVICE_ERROR;
  if (EFI_ERROR(Status))
  {
    FreePool(*Buffer);
    *Buffer = NULL;
    *Size = 0;
  }
  return Status;
}

STATIC EFI_STATUS
CopyVersion(CONST AC_PLIST_DOCUMENT *Document, CONST CHAR8 *Name, OUT CHAR8 **Text)
{
  CONST AC_PLIST_NODE *Root = AcPlistGetRoot(Document);
  CONST AC_PLIST_NODE *Value;
  CONST CHAR8         *Key;
  CONST CHAR8         *String;
  UINTN                Index;
  UINTN                Length;

  if (AcPlistGetType(Root) != AcPlistTypeDictionary)
    return EFI_COMPROMISED_DATA;
  for (Index = 0; Index < AcPlistGetCount(Root); ++Index)
  {
    if (EFI_ERROR(AcPlistGetDictionaryItem(Root, Index, &Key, &Value)))
      return EFI_COMPROMISED_DATA;
    if (AsciiStrCmp(Key, Name) != 0)
      continue;
    String = AcPlistGetString(Value);
    if (String == NULL)
      return EFI_COMPROMISED_DATA;
    Length = AsciiStrnLenS(String, 65);
    if (Length == 0 || Length > 64)
      return EFI_COMPROMISED_DATA;
    for (Index = 0; Index < Length; ++Index)
      if (String[Index] < 0x20 || String[Index] > 0x7E)
        return EFI_COMPROMISED_DATA;
    *Text = AllocateCopyPool(Length + 1, String);
    return *Text == NULL ? EFI_OUT_OF_RESOURCES : EFI_SUCCESS;
  }
  return EFI_NOT_FOUND;
}

STATIC EFI_STATUS
ReadVersionPlist(EFI_FILE_PROTOCOL *Root, CONST CHAR16 *Directory, CONST CHAR16 *Name,
                 OUT AC_PLIST_DOCUMENT **Document)
{
  CHAR16    *Path;
  VOID      *Buffer = NULL;
  UINTN      Size;
  EFI_STATUS Status;

  *Document = NULL;
  Path = InternalJoinBootPath(Directory, Name);
  if (Path == NULL)
    return EFI_OUT_OF_RESOURCES;
  Status = InternalReadBootFile(Root, Path, AC_BOOT_VERSION_LIMIT, &Buffer, &Size);
  if (!EFI_ERROR(Status))
    Status = AcPlistParse(Buffer, Size, Document);
  if (EFI_ERROR(Status))
    DEBUG((DEBUG_WARN, "ACBM: Recovery metadata %s: %r\n", Path, Status));
  if (Buffer != NULL)
    FreePool(Buffer);
  FreePool(Path);
  return Status;
}

STATIC EFI_STATUS
ReadVersions(EFI_FILE_PROTOCOL *Root, IN OUT AC_BOOT_RESOURCES *Resources)
{
  AC_PLIST_DOCUMENT *Document;
  EFI_STATUS         Status;

  Status = ReadVersionPlist(Root, Resources->IscDirectoryPath, L"SystemVersion.plist", &Document);
  if (EFI_ERROR(Status))
    return Status;
  Status = CopyVersion(Document, "ProductVersion", &Resources->ProductVersion);
  if (!EFI_ERROR(Status))
    Status = CopyVersion(Document, "ProductBuildVersion", &Resources->ProductBuildVersion);
  AcPlistFree(Document);
  if (EFI_ERROR(Status))
    return Status;
  Status = ReadVersionPlist(Root, Resources->IscDirectoryPath, L"RestoreVersion.plist", &Document);
  if (EFI_ERROR(Status))
    return Status;
  Status = CopyVersion(Document, "RestoreVersion", &Resources->RestoreVersion);
  AcPlistFree(Document);
  return Status;
}

STATIC EFI_STATUS
FindIsc(CONST AC_BOOT_APFS_VOLUME *Source, CONST AC_BOOT_APFS_VOLUME *Volumes, UINTN VolumeCount,
        CONST VOID *Ticket, UINTN TicketSize, IN OUT AC_BOOT_RESOURCES *Resources)
{
  CONST AC_BOOT_APFS_VOLUME *Volume;
  EFI_DEVICE_PATH_PROTOCOL  *Path;
  EFI_FILE_PROTOCOL         *Root;
  EFI_HANDLE                 Handle;
  CHAR16                    *TicketPath;
  VOID                      *IscTicket = NULL;
  UINTN                      IscTicketSize;
  UINT8                      Hash[48];
  UINTN                      Index;
  EFI_STATUS                 Status = EFI_SUCCESS;
  BOOLEAN                    Found = FALSE;

  TicketPath = InternalJoinBootPath(Resources->IscDirectoryPath, L"apticket.der");
  if (TicketPath == NULL)
    return EFI_OUT_OF_RESOURCES;
  for (Index = 0; Index < VolumeCount; ++Index)
  {
    Volume = &Volumes[Index];
    if (Volume->Identity.Role != APFS_VOLUME_ROLE_PREBOOT ||
        !CompareGuid(&Volume->PartitionType, &gAcBootApfsIscPartitionType) ||
        Source->DiskPathSize == 0 || Source->DiskPathSize != Volume->DiskPathSize ||
        CompareMem(Source->DiskPath, Volume->DiskPath, Source->DiskPathSize) != 0)
      continue;
    Status = gBS->HandleProtocol(Volume->Handle, &gEfiDevicePathProtocolGuid, (VOID **)&Path);
    if (EFI_ERROR(Status))
      break;
    Status = InternalOpenBootVolume(Path, APFS_VOLUME_ROLE_PREBOOT, &Root, &Handle);
    if (EFI_ERROR(Status))
      break;
    if (Handle != Volume->Handle)
      Status = EFI_COMPROMISED_DATA;
    else
      Status = ReadTicket(Root, TicketPath, &IscTicket, &IscTicketSize, Hash);
    if (Status == EFI_NOT_FOUND)
    {
      Root->Close(Root);
      Status = EFI_SUCCESS;
      continue;
    }
    if (!EFI_ERROR(Status) &&
        (TicketSize != IscTicketSize || CompareMem(Ticket, IscTicket, TicketSize) != 0))
      Status = EFI_COMPROMISED_DATA;
    if (IscTicket != NULL)
    {
      FreePool(IscTicket);
      IscTicket = NULL;
    }
    if (!EFI_ERROR(Status) && Found)
    {
      DEBUG((DEBUG_WARN, "ACBM: Ambiguous same-disk Recovery iSC metadata\n"));
      Status = EFI_COMPROMISED_DATA;
    }
    if (!EFI_ERROR(Status))
    {
      Resources->IscDevicePath = DuplicateDevicePath(Path);
      Status =
        Resources->IscDevicePath == NULL ? EFI_OUT_OF_RESOURCES : ReadVersions(Root, Resources);
    }
    Root->Close(Root);
    if (EFI_ERROR(Status))
      break;
    Found = TRUE;
  }
  FreePool(TicketPath);
  if (EFI_ERROR(Status))
    return Status;
  return Found ? EFI_SUCCESS : EFI_NOT_FOUND;
}

EFI_STATUS
InternalResolveRecoveryResources(EFI_HANDLE Source, EFI_FILE_PROTOCOL *Root,
                                 CONST CHAR16 *DirectoryPath, OUT AC_BOOT_RESOURCES *Resources)
{
  AC_BOOT_APFS_VOLUME       *Volumes = NULL;
  CONST AC_BOOT_APFS_VOLUME *Recovery = NULL;
  UINTN                      VolumeCount;
  UINTN                      Index;
  UINTN                      Prefix = StrLen(AC_RECOVERY_BOOT_DIRECTORY);
  CHAR16                     HashText[97];
  STATIC CONST CHAR16        Hex[] = L"0123456789ABCDEF";
  VOID                      *Ticket = NULL;
  UINTN                      TicketSize;
  UINT8                      Hash[48];
  EFI_STATUS                 Status;
  struct
  {
    CONST CHAR16 *Name;
    CHAR16      **Path;
  } Files[] = {
    {L"System\\Library\\Caches\\com.apple.kernelcaches\\kernelcache", &Resources->KernelPath},
    {L"System\\Library\\Caches\\apticket.der", &Resources->TicketPath},
    {L"usr\\standalone\\firmware\\arm64eBaseSystem.dmg", &Resources->BaseSystemPath},
    {L"usr\\standalone\\firmware\\FUD\\BaseSystemTrustCache.img4",
     &Resources->BaseSystemTrustCachePath},
    {L"usr\\standalone\\firmware\\FUD\\StaticTrustCache.img4", &Resources->StaticTrustCachePath},
    {L"usr\\standalone\\firmware\\base_system_root_hash.img4", &Resources->BaseSystemRootHashPath},
    {L"usr\\standalone\\firmware\\root_hash.img4", &Resources->RootHashPath}};

  ZeroMem(Resources, sizeof(*Resources));
  if (StrLen(DirectoryPath) != Prefix + 1 + 96 ||
      StrnCmp(DirectoryPath, AC_RECOVERY_BOOT_DIRECTORY, Prefix) != 0 ||
      DirectoryPath[Prefix] != L'\\' || !ParseHash(DirectoryPath + Prefix + 1, Hash))
    return EFI_INVALID_PARAMETER;
  CopyMem(Resources->BootManifestHash, Hash, sizeof(Hash));
  Status = InternalCollectBootVolumes(&Volumes, &VolumeCount);
  if (EFI_ERROR(Status))
    goto Done;
  for (Index = 0; Index < VolumeCount; ++Index)
    if (Volumes[Index].Handle == Source &&
        Volumes[Index].Identity.Role == APFS_VOLUME_ROLE_RECOVERY &&
        CompareGuid(&Volumes[Index].PartitionType, &gAcBootApfsRecoveryPartitionType))
      Recovery = &Volumes[Index];
  if (Recovery == NULL)
  {
    Status = EFI_UNSUPPORTED;
    goto Done;
  }
  Resources->BootDirectory = AllocateCopyPool(StrSize(DirectoryPath), DirectoryPath);
  if (Resources->BootDirectory == NULL)
  {
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }
  for (Index = 0; Index < ARRAY_SIZE(Files); ++Index)
  {
    Status = LocateFile(Root, DirectoryPath, Files[Index].Name, Files[Index].Path);
    if (EFI_ERROR(Status))
      goto Done;
  }
  Status = ReadTicket(Root, Resources->TicketPath, &Ticket, &TicketSize, Hash);
  if (EFI_ERROR(Status))
    goto Done;
  if (CompareMem(Hash, Resources->BootManifestHash, sizeof(Hash)) != 0)
  {
    Status = EFI_COMPROMISED_DATA;
    goto Done;
  }
  for (Index = 0; Index < sizeof(Hash); ++Index)
  {
    HashText[Index * 2] = Hex[Hash[Index] >> 4];
    HashText[Index * 2 + 1] = Hex[Hash[Index] & 15];
  }
  HashText[96] = L'\0';
  Resources->IscDirectoryPath = InternalJoinBootPath(L"\\SystemRecovery", HashText);
  if (Resources->IscDirectoryPath == NULL)
  {
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }
  Status = FindIsc(Recovery, Volumes, VolumeCount, Ticket, TicketSize, Resources);
  if (EFI_ERROR(Status))
    DEBUG((DEBUG_WARN, "ACBM: Recovery iSC association %s: %r\n", HashText, Status));

Done:
  if (Volumes != NULL)
    FreePool(Volumes);
  if (Ticket != NULL)
    FreePool(Ticket);
  if (EFI_ERROR(Status))
    AcFreeBootResources(Resources);
  return Status;
}

EFI_STATUS
InternalScanRecovery(CONST AC_BOOT_APFS_VOLUME *Volume, IN OUT AC_BOOT_ENTRY **Entries,
                     IN OUT UINTN *Count)
{
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
  EFI_FILE_PROTOCOL               *Root;
  EFI_FILE_PROTOCOL               *Directory;
  EFI_FILE_INFO                   *Info = NULL;
  AC_BOOT_RESOURCES                Resources;
  UINT8                            Hash[48];
  CHAR16                          *Path;
  CHAR8                            Name[160];
  EFI_STATUS                       Status;

  Status =
    gBS->HandleProtocol(Volume->Handle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&FileSystem);
  if (EFI_ERROR(Status))
    return Status;
  Status = FileSystem->OpenVolume(FileSystem, &Root);
  if (EFI_ERROR(Status))
    return Status;
  Status = Root->Open(Root, &Directory, AC_RECOVERY_BOOT_DIRECTORY, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(Status))
  {
    Root->Close(Root);
    return Status == EFI_NOT_FOUND ? EFI_SUCCESS : Status;
  }
  Status = InternalGetBootFileInfo(Directory, &Info);
  if (!EFI_ERROR(Status) && (Info->Attribute & EFI_FILE_DIRECTORY) == 0)
    Status = EFI_VOLUME_CORRUPTED;
  if (Info != NULL)
    FreePool(Info);
  while (!EFI_ERROR(Status))
  {
    Status = InternalReadBootDirectory(Directory, &Info);
    if (EFI_ERROR(Status) || Info == NULL)
      break;
    if ((Info->Attribute & EFI_FILE_DIRECTORY) != 0 && ParseHash(Info->FileName, Hash))
    {
      Path = InternalJoinBootPath(AC_RECOVERY_BOOT_DIRECTORY, Info->FileName);
      if (Path == NULL)
        Status = EFI_OUT_OF_RESOURCES;
      else
      {
        Status = InternalResolveRecoveryResources(Volume->Handle, Root, Path, &Resources);
        if (!EFI_ERROR(Status))
        {
          AsciiSPrint(Name, sizeof(Name), "Recovery %a (%a)", Resources.ProductVersion,
                      Resources.ProductBuildVersion);
          Status = InternalAppendBootEntry(Volume, AcBootEntryRecovery, Name, Path, Entries, Count);
          AcFreeBootResources(&Resources);
        }
        if (EFI_ERROR(Status))
          DEBUG((DEBUG_WARN, "ACBM: Recovery %p target %s: %r\n", Volume->Handle, Path, Status));
        FreePool(Path);
      }
    }
    FreePool(Info);
    if (Status == EFI_OUT_OF_RESOURCES || Status == EFI_NO_MEDIA || Status == EFI_MEDIA_CHANGED)
      break;
    Status = EFI_SUCCESS;
  }
  Directory->Close(Directory);
  Root->Close(Root);
  return Status;
}
