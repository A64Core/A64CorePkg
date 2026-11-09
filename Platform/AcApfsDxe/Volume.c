/** @file
  Partition reads, APFS volume children, and protocol publication.
**/

#include "ApfsDxe.h"

#pragma pack(push, 1)
typedef struct
{
  VENDOR_DEVICE_PATH Vendor;
  UINT8              Uuid[16];
} APFS_VOLUME_DEVICE_PATH;
#pragma pack(pop)

STATIC CONST EFI_GUID mVolumeDevicePathGuid = {
  0xD2BDBE73, 0xF833, 0x4F80, {0xA3, 0x1E, 0x14, 0x16, 0x19, 0x6F, 0x81, 0x17}};

EFI_STATUS
ApfsDxeMapStatus(EFI_STATUS Status)
{
  return Status == EFI_COMPROMISED_DATA ? EFI_VOLUME_CORRUPTED : Status;
}

EFI_STATUS
ApfsDxeCheckMedia(APFS_DXE_CONTAINER *Container)
{
  EFI_BLOCK_IO_MEDIA *Media = Container->BlockIo->Media;

  if (Media == NULL || !Media->MediaPresent)
  {
    Container->Invalid = TRUE;
    return EFI_NO_MEDIA;
  }
  if (Container->Invalid || Media->MediaId != Container->MediaId || Media->BlockSize == 0 ||
      Media->LastBlock == MAX_UINT64 || Media->LastBlock + 1 > MAX_UINT64 / Media->BlockSize ||
      (Media->LastBlock + 1) * Media->BlockSize != Container->Size)
  {
    Container->Invalid = TRUE;
    return EFI_MEDIA_CHANGED;
  }
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI
ApfsDxeReadDisk(VOID *Context, UINT64 Offset, UINTN Size, VOID *Buffer)
{
  APFS_DXE_CONTAINER *Container = Context;
  EFI_STATUS          Status = ApfsDxeCheckMedia(Container);

  if (EFI_ERROR(Status))
    return Status;
  if (Offset > Container->Size || Size > Container->Size - Offset)
    return EFI_DEVICE_ERROR;
  Status = Container->DiskIo->ReadDisk(Container->DiskIo, Container->MediaId, Offset, Size, Buffer);
  if (Status == EFI_MEDIA_CHANGED || Status == EFI_NO_MEDIA)
    Container->Invalid = TRUE;
  return Status;
}

EFI_STATUS
ApfsDxeCreateVolume(APFS_DXE_CONTAINER *Container, UINT32 Index,
                    EFI_DEVICE_PATH_PROTOCOL *ParentPath, OUT APFS_DXE_VOLUME **Volume)
{
  AC_APFS_VOLUME_INFO     Info;
  APFS_VOLUME_DEVICE_PATH Node;
  APFS_DXE_VOLUME        *Result;
  EFI_STATUS              Status;

  *Volume = NULL;
  Status = AcApfsGetVolumeInfo(Container->Apfs, Index, &Info);
  if (EFI_ERROR(Status))
    return ApfsDxeMapStatus(Status);
  Result = AllocateZeroPool(sizeof(*Result));
  if (Result == NULL)
    return EFI_OUT_OF_RESOURCES;
  Result->Container = Container;
  Result->Identity.Revision = AC_APFS_VOLUME_INFO_PROTOCOL_REVISION;
  Result->Identity.ContainerHandle = Container->Controller;
  CopyMem(Result->Identity.ContainerUuid, Container->Info.Uuid, sizeof(Container->Info.Uuid));
  CopyMem(Result->Identity.VolumeUuid, Info.Uuid, sizeof(Info.Uuid));
  CopyMem(Result->Identity.GroupUuid, Info.GroupUuid, sizeof(Info.GroupUuid));
  Result->Identity.Role = Info.Role;
  CopyMem(Result->Identity.Name, Info.Name, sizeof(Info.Name));
  Status = ApfsDxeUtf8ToUnicode(Info.Name, &Result->Label);
  if (EFI_ERROR(Status))
    goto Failure;
  ZeroMem(&Node, sizeof(Node));
  Node.Vendor.Header.Type = MEDIA_DEVICE_PATH;
  Node.Vendor.Header.SubType = MEDIA_VENDOR_DP;
  SetDevicePathNodeLength(&Node.Vendor.Header, sizeof(Node));
  CopyMem(&Node.Vendor.Guid, &mVolumeDevicePathGuid, sizeof(mVolumeDevicePathGuid));
  CopyMem(Node.Uuid, Info.Uuid, sizeof(Node.Uuid));
  Result->DevicePath = AppendDevicePathNode(ParentPath, &Node.Vendor.Header);
  if (Result->DevicePath == NULL)
  {
    Status = EFI_OUT_OF_RESOURCES;
    goto Failure;
  }
  Status = AcApfsOpenVolume(Container->Apfs, Index, &Result->Apfs);
  if (EFI_ERROR(Status) && Status != EFI_UNSUPPORTED)
  {
    Status = ApfsDxeMapStatus(Status);
    goto Failure;
  }
  Result->FileSystem.Revision = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_REVISION;
  Result->FileSystem.OpenVolume = ApfsDxeOpenVolume;
  *Volume = Result;
  return EFI_SUCCESS;

Failure:
  ApfsDxeFreeVolume(Result);
  return Status;
}

EFI_STATUS
ApfsDxePublishVolume(APFS_DXE_VOLUME *Volume)
{
  EFI_DISK_IO_PROTOCOL *DiskIo;
  EFI_STATUS            Status;

  Status = gBS->InstallMultipleProtocolInterfaces(
    &Volume->Handle, &gEfiDevicePathProtocolGuid, Volume->DevicePath,
    &gAcApfsVolumeInfoProtocolGuid, &Volume->Identity, NULL);
  if (EFI_ERROR(Status))
    return Status;
  Volume->Published = TRUE;
  Status = gBS->OpenProtocol(Volume->Container->Controller, &gEfiDiskIoProtocolGuid,
                             (VOID **)&DiskIo, gAcApfsDriverBinding.DriverBindingHandle,
                             Volume->Handle, EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER);
  if (EFI_ERROR(Status))
    return Status;
  Volume->ChildOpen = TRUE;
  if (Volume->Apfs != NULL)
  {
    Status = gBS->InstallMultipleProtocolInterfaces(
      &Volume->Handle, &gEfiSimpleFileSystemProtocolGuid, &Volume->FileSystem, NULL);
    if (EFI_ERROR(Status))
      return Status;
    Volume->FileSystemInstalled = TRUE;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
ApfsDxeRemoveVolume(APFS_DXE_VOLUME *Volume)
{
  EFI_DISK_IO_PROTOCOL *DiskIo;
  EFI_STATUS            Status;
  EFI_STATUS            Restore;
  BOOLEAN               WasChild;

  if (Volume->OpenFiles != 0)
    return EFI_ACCESS_DENIED;
  WasChild = Volume->ChildOpen;
  if (WasChild)
  {
    Status = gBS->CloseProtocol(Volume->Container->Controller, &gEfiDiskIoProtocolGuid,
                                gAcApfsDriverBinding.DriverBindingHandle, Volume->Handle);
    if (EFI_ERROR(Status))
      return Status;
    Volume->ChildOpen = FALSE;
  }
  if (!Volume->Published)
    return EFI_SUCCESS;
  if (Volume->FileSystemInstalled)
    Status = gBS->UninstallMultipleProtocolInterfaces(
      Volume->Handle, &gEfiSimpleFileSystemProtocolGuid, &Volume->FileSystem,
      &gAcApfsVolumeInfoProtocolGuid, &Volume->Identity, &gEfiDevicePathProtocolGuid,
      Volume->DevicePath, NULL);
  else
    Status = gBS->UninstallMultipleProtocolInterfaces(
      Volume->Handle, &gAcApfsVolumeInfoProtocolGuid, &Volume->Identity,
      &gEfiDevicePathProtocolGuid, Volume->DevicePath, NULL);
  if (EFI_ERROR(Status))
  {
    if (WasChild)
    {
      Restore = gBS->OpenProtocol(Volume->Container->Controller, &gEfiDiskIoProtocolGuid,
                                  (VOID **)&DiskIo, gAcApfsDriverBinding.DriverBindingHandle,
                                  Volume->Handle, EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER);
      Volume->ChildOpen = !EFI_ERROR(Restore);
      if (EFI_ERROR(Restore))
        return Restore;
    }
    return Status;
  }
  Volume->Published = FALSE;
  Volume->FileSystemInstalled = FALSE;
  Volume->Handle = NULL;
  return EFI_SUCCESS;
}

VOID
ApfsDxeFreeVolume(APFS_DXE_VOLUME *Volume)
{
  ASSERT(!Volume->Published && !Volume->ChildOpen && Volume->OpenFiles == 0);
  AcApfsCloseVolume(Volume->Apfs);
  if (Volume->DevicePath != NULL)
    FreePool(Volume->DevicePath);
  if (Volume->Label != NULL)
    FreePool(Volume->Label);
  FreePool(Volume);
}

EFI_STATUS EFIAPI
ApfsDxeOpenVolume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This, EFI_FILE_PROTOCOL **Root)
{
  APFS_DXE_VOLUME *Volume;
  CHAR8           *Path;
  EFI_STATUS       Status;

  if (Root == NULL)
    return EFI_INVALID_PARAMETER;
  *Root = NULL;
  if (This == NULL)
    return EFI_INVALID_PARAMETER;
  Volume = BASE_CR(This, APFS_DXE_VOLUME, FileSystem);
  Path = AllocateZeroPool(2);
  if (Path == NULL)
    return EFI_OUT_OF_RESOURCES;
  Path[0] = '/';
  Status = ApfsDxeOpenFile(Volume, Path, Root);
  if (EFI_ERROR(Status))
    FreePool(Path);
  return Status;
}
