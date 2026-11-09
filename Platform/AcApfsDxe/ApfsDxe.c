/** @file
  APFS driver entry, controller binding, and disconnect handling.
**/

#include "ApfsDxe.h"

STATIC LIST_ENTRY mContainers = INITIALIZE_LIST_HEAD_VARIABLE(mContainers);

STATIC APFS_DXE_CONTAINER *
FindContainer(EFI_HANDLE Controller)
{
  LIST_ENTRY         *Link;
  APFS_DXE_CONTAINER *Container;

  for (Link = GetFirstNode(&mContainers); !IsNull(&mContainers, Link);
       Link = GetNextNode(&mContainers, Link))
  {
    Container = BASE_CR(Link, APFS_DXE_CONTAINER, Link);
    if (Container->Controller == Controller)
      return Container;
  }
  return NULL;
}

STATIC EFI_STATUS
GetMedia(EFI_HANDLE Controller, OUT EFI_BLOCK_IO_PROTOCOL **BlockIo, OUT UINT64 *Size)
{
  EFI_BLOCK_IO_MEDIA *Media;
  EFI_STATUS          Status;

  Status = gBS->HandleProtocol(Controller, &gEfiBlockIoProtocolGuid, (VOID **)BlockIo);
  if (EFI_ERROR(Status))
    return Status;
  Media = (*BlockIo)->Media;
  if (Media == NULL || !Media->MediaPresent)
    return EFI_NO_MEDIA;
  if (!Media->LogicalPartition || Media->BlockSize == 0 || Media->LastBlock == MAX_UINT64 ||
      Media->LastBlock + 1 > MAX_UINT64 / Media->BlockSize)
    return EFI_UNSUPPORTED;
  *Size = (Media->LastBlock + 1) * Media->BlockSize;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
DriverSupported(EFI_DRIVER_BINDING_PROTOCOL *This, EFI_HANDLE Controller,
                EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath)
{
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  EFI_DISK_IO_PROTOCOL  *DiskIo;
  UINT8                  Prefix[sizeof(APFS_OBJECT) + sizeof(UINT32)];
  UINT32                 Magic;
  UINT64                 Size;
  EFI_STATUS             Status;

  if (RemainingDevicePath != NULL && !IsDevicePathEnd(RemainingDevicePath))
    return EFI_UNSUPPORTED;
  if (FindContainer(Controller) != NULL)
    return EFI_ALREADY_STARTED;
  Status = GetMedia(Controller, &BlockIo, &Size);
  if (EFI_ERROR(Status))
    return Status;
  if (Size < sizeof(Prefix))
    return EFI_UNSUPPORTED;
  Status = gBS->OpenProtocol(Controller, &gEfiDiskIoProtocolGuid, (VOID **)&DiskIo,
                             This->DriverBindingHandle, Controller, EFI_OPEN_PROTOCOL_BY_DRIVER);
  if (EFI_ERROR(Status))
    return Status;
  Status = DiskIo->ReadDisk(DiskIo, BlockIo->Media->MediaId, 0, sizeof(Prefix), Prefix);
  if (!EFI_ERROR(Status))
  {
    CopyMem(&Magic, Prefix + sizeof(APFS_OBJECT), sizeof(Magic));
    Status = Magic == APFS_NX_MAGIC ? EFI_SUCCESS : EFI_UNSUPPORTED;
  }
  gBS->CloseProtocol(Controller, &gEfiDiskIoProtocolGuid, This->DriverBindingHandle, Controller);
  return Status;
}

STATIC EFI_STATUS
FreeContainer(APFS_DXE_CONTAINER *Container)
{
  EFI_STATUS Status;

  ASSERT(IsListEmpty(&Container->Volumes));
  Status = gBS->CloseProtocol(Container->Controller, &gEfiDiskIoProtocolGuid,
                              gAcApfsDriverBinding.DriverBindingHandle, Container->Controller);
  if (EFI_ERROR(Status))
    return Status;
  RemoveEntryList(&Container->Link);
  AcApfsCloseContainer(Container->Apfs);
  FreePool(Container);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
DriverStart(EFI_DRIVER_BINDING_PROTOCOL *This, EFI_HANDLE Controller,
            EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath)
{
  APFS_DXE_CONTAINER       *Container;
  APFS_DXE_VOLUME          *Volume;
  EFI_DEVICE_PATH_PROTOCOL *DevicePath;
  AC_APFS_IO                Io;
  LIST_ENTRY               *Link;
  LIST_ENTRY               *Next;
  EFI_STATUS                Status;
  EFI_STATUS                Cleanup;
  UINT32                    Index;

  if (RemainingDevicePath != NULL && !IsDevicePathEnd(RemainingDevicePath))
    return EFI_UNSUPPORTED;
  if (FindContainer(Controller) != NULL)
    return EFI_ALREADY_STARTED;
  Container = AllocateZeroPool(sizeof(*Container));
  if (Container == NULL)
    return EFI_OUT_OF_RESOURCES;
  Container->Controller = Controller;
  InitializeListHead(&Container->Volumes);
  Status = GetMedia(Controller, &Container->BlockIo, &Container->Size);
  if (EFI_ERROR(Status))
    goto Failure;
  Container->MediaId = Container->BlockIo->Media->MediaId;
  Status = gBS->HandleProtocol(Controller, &gEfiDevicePathProtocolGuid, (VOID **)&DevicePath);
  if (EFI_ERROR(Status))
    goto Failure;
  Status = gBS->OpenProtocol(Controller, &gEfiDiskIoProtocolGuid, (VOID **)&Container->DiskIo,
                             This->DriverBindingHandle, Controller, EFI_OPEN_PROTOCOL_BY_DRIVER);
  if (EFI_ERROR(Status))
    goto Failure;
  InsertTailList(&mContainers, &Container->Link);
  Io.Context = Container;
  Io.Size = Container->Size;
  Io.Read = ApfsDxeReadDisk;
  Status = AcApfsOpenContainer(&Io, &Container->Apfs);
  if (EFI_ERROR(Status))
  {
    Status = ApfsDxeMapStatus(Status);
    goto Rollback;
  }
  Status = AcApfsGetContainerInfo(Container->Apfs, &Container->Info);
  if (EFI_ERROR(Status))
    goto Rollback;
  for (Index = 0; Index < Container->Info.VolumeCount; ++Index)
  {
    Status = ApfsDxeCreateVolume(Container, Index, DevicePath, &Volume);
    if (EFI_ERROR(Status))
      goto Rollback;
    InsertTailList(&Container->Volumes, &Volume->Link);
  }
  for (Link = GetFirstNode(&Container->Volumes); !IsNull(&Container->Volumes, Link);
       Link = GetNextNode(&Container->Volumes, Link))
  {
    Status = ApfsDxePublishVolume(BASE_CR(Link, APFS_DXE_VOLUME, Link));
    if (EFI_ERROR(Status))
      goto Rollback;
  }
  return EFI_SUCCESS;

Rollback:
  for (Link = GetFirstNode(&Container->Volumes); !IsNull(&Container->Volumes, Link); Link = Next)
  {
    Next = GetNextNode(&Container->Volumes, Link);
    Volume = BASE_CR(Link, APFS_DXE_VOLUME, Link);
    Cleanup = ApfsDxeRemoveVolume(Volume);
    if (!EFI_ERROR(Cleanup))
    {
      RemoveEntryList(Link);
      ApfsDxeFreeVolume(Volume);
    }
  }
  // Published children must remain managed if a consumer prevents rollback.
  if (!IsListEmpty(&Container->Volumes))
  {
    DEBUG((DEBUG_ERROR, "APFS: Start rollback blocked after %r\n", Status));
    return EFI_SUCCESS;
  }
  Cleanup = FreeContainer(Container);
  if (EFI_ERROR(Cleanup))
    return Cleanup;
  return Status;

Failure:
  FreePool(Container);
  return Status;
}

STATIC EFI_STATUS EFIAPI
DriverStop(EFI_DRIVER_BINDING_PROTOCOL *This, EFI_HANDLE Controller, UINTN NumberOfChildren,
           EFI_HANDLE *ChildHandleBuffer)
{
  APFS_DXE_CONTAINER *Container;
  APFS_DXE_VOLUME    *Volume;
  LIST_ENTRY         *Link;
  UINTN               Index;
  EFI_STATUS          Status;

  (VOID) This;
  Container = FindContainer(Controller);
  if (Container == NULL || (NumberOfChildren != 0 && ChildHandleBuffer == NULL))
    return EFI_INVALID_PARAMETER;
  if (NumberOfChildren == 0)
    return IsListEmpty(&Container->Volumes) ? FreeContainer(Container) : EFI_ACCESS_DENIED;
  for (Index = 0; Index < NumberOfChildren; ++Index)
  {
    for (Link = GetFirstNode(&Container->Volumes); !IsNull(&Container->Volumes, Link);
         Link = GetNextNode(&Container->Volumes, Link))
    {
      Volume = BASE_CR(Link, APFS_DXE_VOLUME, Link);
      if (Volume->Handle == ChildHandleBuffer[Index])
        break;
    }
    if (IsNull(&Container->Volumes, Link))
      return EFI_INVALID_PARAMETER;
    Volume = BASE_CR(Link, APFS_DXE_VOLUME, Link);
    Status = ApfsDxeRemoveVolume(Volume);
    if (EFI_ERROR(Status))
      return Status;
    RemoveEntryList(Link);
    ApfsDxeFreeVolume(Volume);
  }
  return EFI_SUCCESS;
}

EFI_DRIVER_BINDING_PROTOCOL gAcApfsDriverBinding = {
  DriverSupported, DriverStart, DriverStop, 0x10, NULL, NULL};

EFI_STATUS EFIAPI
AcApfsDxeEntryPoint(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  (VOID) SystemTable;
  gAcApfsDriverBinding.ImageHandle = ImageHandle;
  gAcApfsDriverBinding.DriverBindingHandle = ImageHandle;
  return gBS->InstallMultipleProtocolInterfaces(&gAcApfsDriverBinding.DriverBindingHandle,
                                                &gEfiDriverBindingProtocolGuid,
                                                &gAcApfsDriverBinding, NULL);
}

EFI_STATUS EFIAPI
AcApfsDxeUnload(EFI_HANDLE ImageHandle)
{
  APFS_DXE_CONTAINER *Container;
  EFI_HANDLE          Controller;
  EFI_STATUS          Status;

  while (!IsListEmpty(&mContainers))
  {
    Container = BASE_CR(GetFirstNode(&mContainers), APFS_DXE_CONTAINER, Link);
    Controller = Container->Controller;
    Status = gBS->DisconnectController(Controller, gAcApfsDriverBinding.DriverBindingHandle, NULL);
    if (EFI_ERROR(Status))
      return Status;
    if (FindContainer(Controller) != NULL)
      return EFI_ACCESS_DENIED;
  }
  return gBS->UninstallMultipleProtocolInterfaces(ImageHandle, &gEfiDriverBindingProtocolGuid,
                                                  &gAcApfsDriverBinding, NULL);
}
