/** @file
  APFS volume inventory and boot entry discovery.
**/

#include "BootManagementInternal.h"

STATIC VOID
FreeEntry(AC_BOOT_ENTRY *Entry)
{
  if (Entry->Name != NULL)
    FreePool(Entry->Name);
  if (Entry->DevicePath != NULL)
    FreePool(Entry->DevicePath);
  if (Entry->DirectoryPath != NULL)
    FreePool(Entry->DirectoryPath);
  ZeroMem(Entry, sizeof(*Entry));
}

VOID
AcFreeBootEntries(IN AC_BOOT_ENTRY *Entries OPTIONAL, IN UINTN EntryCount)
{
  UINTN Index;

  if (Entries == NULL)
    return;
  for (Index = 0; Index < EntryCount; ++Index)
    FreeEntry(&Entries[Index]);
  FreePool(Entries);
}

EFI_STATUS
InternalCollectBootVolumes(OUT AC_BOOT_APFS_VOLUME **Volumes, OUT UINTN *VolumeCount)
{
  EFI_HANDLE                   *Handles;
  AC_APFS_VOLUME_INFO_PROTOCOL *Identity;
  UINTN                         Count;
  UINTN                         Index;
  EFI_STATUS                    Status;

  *Volumes = NULL;
  *VolumeCount = 0;
  Status =
    gBS->LocateHandleBuffer(ByProtocol, &gAcApfsVolumeInfoProtocolGuid, NULL, &Count, &Handles);
  if (Status == EFI_NOT_FOUND)
    return EFI_SUCCESS;
  if (EFI_ERROR(Status))
    return Status;
  if (Count > MAX_UINTN / sizeof(**Volumes))
  {
    FreePool(Handles);
    return EFI_OUT_OF_RESOURCES;
  }
  *Volumes = AllocateZeroPool(Count * sizeof(**Volumes));
  if (*Volumes == NULL)
  {
    FreePool(Handles);
    return EFI_OUT_OF_RESOURCES;
  }
  for (Index = 0; Index < Count; ++Index)
  {
    Status =
      gBS->HandleProtocol(Handles[Index], &gAcApfsVolumeInfoProtocolGuid, (VOID **)&Identity);
    if (EFI_ERROR(Status))
    {
      if (Status == EFI_OUT_OF_RESOURCES)
        break;
      DEBUG((DEBUG_WARN, "ACBM: Volume %p identity: %r\n", Handles[Index], Status));
      continue;
    }
    if (Identity->Revision != AC_APFS_VOLUME_INFO_PROTOCOL_REVISION ||
        Identity->ContainerHandle == NULL ||
        AsciiStrnLenS(Identity->Name, sizeof(Identity->Name)) == sizeof(Identity->Name))
    {
      DEBUG((DEBUG_WARN, "ACBM: Volume %p has an unsupported identity\n", Handles[Index]));
      continue;
    }
    (*Volumes)[*VolumeCount].Handle = Handles[Index];
    CopyMem(&(*Volumes)[*VolumeCount].Identity, Identity, sizeof(*Identity));
    Status = InternalGetBootPartition(
      Identity->ContainerHandle, &(*Volumes)[*VolumeCount].PartitionType,
      &(*Volumes)[*VolumeCount].DiskPath, &(*Volumes)[*VolumeCount].DiskPathSize);
    if (Status == EFI_OUT_OF_RESOURCES)
      break;
    if (EFI_ERROR(Status))
      DEBUG((DEBUG_WARN, "ACBM: Volume %p partition identity: %r\n", Handles[Index], Status));
    ++*VolumeCount;
  }
  FreePool(Handles);
  if (Status == EFI_OUT_OF_RESOURCES)
  {
    FreePool(*Volumes);
    *Volumes = NULL;
    *VolumeCount = 0;
    return Status;
  }
  return EFI_SUCCESS;
}

STATIC CONST AC_BOOT_APFS_VOLUME *
FindSystem(CONST AC_BOOT_APFS_VOLUME *Volumes, UINTN Count, CONST AC_BOOT_APFS_VOLUME *Preboot,
           CONST UINT8 GroupUuid[16])
{
  STATIC CONST UINT8         ZeroUuid[16];
  CONST AC_BOOT_APFS_VOLUME *System = NULL;
  UINTN                      Systems = 0;
  UINTN                      Data = 0;
  UINTN                      Index;

  if (CompareMem(GroupUuid, ZeroUuid, 16) == 0)
    return NULL;
  for (Index = 0; Index < Count; ++Index)
  {
    if (Volumes[Index].Identity.ContainerHandle != Preboot->Identity.ContainerHandle ||
        CompareMem(Volumes[Index].Identity.ContainerUuid, Preboot->Identity.ContainerUuid, 16) !=
          0 ||
        CompareMem(Volumes[Index].Identity.GroupUuid, GroupUuid, 16) != 0)
      continue;
    if (Volumes[Index].Identity.Role == APFS_VOLUME_ROLE_SYSTEM)
    {
      System = &Volumes[Index];
      ++Systems;
    }
    else if (Volumes[Index].Identity.Role == APFS_VOLUME_ROLE_DATA)
      ++Data;
  }
  return Systems == 1 && Data == 1 ? System : NULL;
}

STATIC EFI_STATUS
DecodeName(CONST CHAR8 *Name, OUT CHAR16 **Text)
{
  CONST UINT8 *Bytes = (CONST UINT8 *)Name;
  UINTN        Length = AsciiStrLen(Name);
  UINTN        Index = 0;
  UINTN        Count = 0;
  UINTN        Tail;
  UINT32       Point;
  UINT32       Minimum;

  *Text = AllocateZeroPool((Length + 1) * sizeof(CHAR16));
  if (*Text == NULL)
    return EFI_OUT_OF_RESOURCES;
  while (Index < Length)
  {
    Point = Bytes[Index++];
    Tail = 0;
    Minimum = 0;
    if (Point >= 0xC2 && Point <= 0xDF)
    {
      Point &= 0x1F;
      Tail = 1;
      Minimum = 0x80;
    }
    else if (Point >= 0xE0 && Point <= 0xEF)
    {
      Point &= 0xF;
      Tail = 2;
      Minimum = 0x800;
    }
    else if (Point >= 0xF0 && Point <= 0xF4)
    {
      Point &= 7;
      Tail = 3;
      Minimum = 0x10000;
    }
    else if (Point >= 0x80)
      goto Invalid;
    if (Tail > Length - Index)
      goto Invalid;
    while (Tail-- != 0)
    {
      if ((Bytes[Index] & 0xC0) != 0x80)
        goto Invalid;
      Point = (Point << 6) | (Bytes[Index++] & 0x3F);
    }
    if (Point < Minimum || Point > 0x10FFFF || (Point >= 0xD800 && Point <= 0xDFFF))
      goto Invalid;
    if (Point >= 0x10000)
    {
      Point -= 0x10000;
      (*Text)[Count++] = (CHAR16)(0xD800 | (Point >> 10));
      (*Text)[Count++] = (CHAR16)(0xDC00 | (Point & 0x3FF));
    }
    else
      (*Text)[Count++] = (CHAR16)Point;
  }
  return EFI_SUCCESS;

Invalid:
  FreePool(*Text);
  *Text = NULL;
  return EFI_VOLUME_CORRUPTED;
}

EFI_STATUS
InternalAppendBootEntry(CONST AC_BOOT_APFS_VOLUME *Volume, AC_BOOT_ENTRY_TYPE Type,
                        CONST CHAR8 *Name, CONST CHAR16 *DirectoryPath,
                        IN OUT AC_BOOT_ENTRY **Entries, IN OUT UINTN *Count)
{
  AC_BOOT_ENTRY             Entry = {0};
  AC_BOOT_ENTRY            *Array;
  EFI_DEVICE_PATH_PROTOCOL *DevicePath;
  EFI_STATUS                Status;

  Status = gBS->HandleProtocol(Volume->Handle, &gEfiDevicePathProtocolGuid, (VOID **)&DevicePath);
  if (EFI_ERROR(Status))
    return Status;
  if (DevicePath == NULL || !IsDevicePathValid(DevicePath, 0))
    return EFI_VOLUME_CORRUPTED;
  Entry.Type = Type;
  Status = DecodeName(Name, &Entry.Name);
  if (EFI_ERROR(Status))
    return Status;
  Entry.DevicePath = DuplicateDevicePath(DevicePath);
  Entry.DirectoryPath = AllocateCopyPool(StrSize(DirectoryPath), DirectoryPath);
  CopyMem(Entry.ContainerUuid, Volume->Identity.ContainerUuid, sizeof(Entry.ContainerUuid));
  CopyMem(Entry.VolumeUuid, Volume->Identity.VolumeUuid, sizeof(Entry.VolumeUuid));
  if (Entry.DevicePath == NULL || Entry.DirectoryPath == NULL ||
      *Count >= MAX_UINTN / sizeof(**Entries))
  {
    Status = EFI_OUT_OF_RESOURCES;
    goto Failure;
  }
  Array = ReallocatePool(*Count * sizeof(**Entries), (*Count + 1) * sizeof(**Entries), *Entries);
  if (Array == NULL)
  {
    Status = EFI_OUT_OF_RESOURCES;
    goto Failure;
  }
  *Entries = Array;
  Array[(*Count)++] = Entry;
  return EFI_SUCCESS;

Failure:
  FreeEntry(&Entry);
  return Status;
}

STATIC EFI_STATUS
ScanPreboot(CONST AC_BOOT_APFS_VOLUME *Volumes, UINTN VolumeCount,
            CONST AC_BOOT_APFS_VOLUME *Preboot, IN OUT AC_BOOT_ENTRY **Entries, IN OUT UINTN *Count)
{
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
  EFI_FILE_PROTOCOL               *Root;
  EFI_FILE_INFO                   *Info;
  CONST AC_BOOT_APFS_VOLUME       *System;
  AC_BOOT_RESOURCES                Resources;
  CHAR16                           Path[38];
  UINT8                            Uuid[16];
  EFI_STATUS                       Status;

  Status =
    gBS->HandleProtocol(Preboot->Handle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&FileSystem);
  if (EFI_ERROR(Status))
    return Status;
  Status = FileSystem->OpenVolume(FileSystem, &Root);
  if (EFI_ERROR(Status))
    return Status;
  for (;;)
  {
    Status = InternalReadBootDirectory(Root, &Info);
    if (EFI_ERROR(Status) || Info == NULL)
      break;
    System = NULL;
    if ((Info->Attribute & EFI_FILE_DIRECTORY) != 0 && InternalParseBootUuid(Info->FileName, Uuid))
      System = FindSystem(Volumes, VolumeCount, Preboot, Uuid);
    if (System != NULL)
    {
      Path[0] = L'\\';
      CopyMem(Path + 1, Info->FileName, 37 * sizeof(CHAR16));
      Status = InternalResolveBootResources(Root, Path, &Resources);
      if (!EFI_ERROR(Status))
      {
        AcFreeBootResources(&Resources);
        Status = InternalAppendBootEntry(
          Preboot, AcBootEntryMacOS,
          System->Identity.Name[0] != '\0' ? System->Identity.Name : "macOS", Path, Entries, Count);
      }
      if (EFI_ERROR(Status))
        DEBUG((DEBUG_WARN, "ACBM: Preboot %p target %s: %r\n", Preboot->Handle, Path, Status));
    }
    FreePool(Info);
    if (Status == EFI_OUT_OF_RESOURCES || Status == EFI_NO_MEDIA || Status == EFI_MEDIA_CHANGED)
      break;
  }
  Root->Close(Root);
  return Status;
}

EFI_STATUS
AcScanBootEntries(OUT AC_BOOT_ENTRY **Entries, OUT UINTN *EntryCount)
{
  AC_BOOT_APFS_VOLUME *Volumes;
  UINTN                VolumeCount;
  UINTN                Index;
  UINTN                InitialCount;
  EFI_STATUS           Status;

  if (Entries == NULL || EntryCount == NULL)
    return EFI_INVALID_PARAMETER;
  *Entries = NULL;
  *EntryCount = 0;
  Status = InternalCollectBootVolumes(&Volumes, &VolumeCount);
  if (EFI_ERROR(Status))
    return Status;
  for (Index = 0; Index < VolumeCount; ++Index)
  {
    InitialCount = *EntryCount;
    if (Volumes[Index].Identity.Role == APFS_VOLUME_ROLE_PREBOOT)
      Status = ScanPreboot(Volumes, VolumeCount, &Volumes[Index], Entries, EntryCount);
    else
      continue;
    if (EFI_ERROR(Status))
    {
      DEBUG((DEBUG_WARN, "ACBM: Volume %p scan: %r\n", Volumes[Index].Handle, Status));
      while (*EntryCount > InitialCount)
        FreeEntry(&(*Entries)[--*EntryCount]);
    }
    if (Status == EFI_OUT_OF_RESOURCES)
      break;
  }
  if (Volumes != NULL)
    FreePool(Volumes);
  if (Status == EFI_OUT_OF_RESOURCES || *EntryCount == 0)
  {
    AcFreeBootEntries(*Entries, *EntryCount);
    *Entries = NULL;
    *EntryCount = 0;
  }
  return Status == EFI_OUT_OF_RESOURCES ? Status : EFI_SUCCESS;
}
