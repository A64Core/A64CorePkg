/** @file
  Standard UEFI file access for boot discovery and resource inspection.
**/

#include "BootManagementInternal.h"

STATIC BOOLEAN
ValidFileInfo(CONST EFI_FILE_INFO *Info, UINTN Size)
{
  UINTN Characters;
  UINTN Index;

  if (Size < SIZE_OF_EFI_FILE_INFO + sizeof(CHAR16) || Info->Size > Size ||
      Info->Size < SIZE_OF_EFI_FILE_INFO + sizeof(CHAR16) ||
      (Info->Size - SIZE_OF_EFI_FILE_INFO) % sizeof(CHAR16) != 0)
    return FALSE;
  Characters = (UINTN)(Info->Size - SIZE_OF_EFI_FILE_INFO) / sizeof(CHAR16);
  for (Index = 0; Index < Characters; ++Index)
    if (Info->FileName[Index] == L'\0')
      return TRUE;
  return FALSE;
}

EFI_STATUS
InternalGetBootPartition(EFI_HANDLE Handle, OUT EFI_GUID *Type,
                         OUT CONST EFI_DEVICE_PATH_PROTOCOL **DiskPath, OUT UINTN *DiskPathSize)
{
  EFI_PARTITION_INFO_PROTOCOL *Partition;
  EFI_DEVICE_PATH_PROTOCOL    *Path;
  EFI_DEVICE_PATH_PROTOCOL    *Node;
  HARDDRIVE_DEVICE_PATH       *HardDrive;
  EFI_STATUS                   Status;

  ZeroMem(Type, sizeof(*Type));
  *DiskPath = NULL;
  *DiskPathSize = 0;
  Status = gBS->HandleProtocol(Handle, &gEfiPartitionInfoProtocolGuid, (VOID **)&Partition);
  if (EFI_ERROR(Status))
    return Status;
  if (Partition->Revision != EFI_PARTITION_INFO_PROTOCOL_REVISION ||
      Partition->Type != PARTITION_TYPE_GPT)
    return EFI_UNSUPPORTED;
  Status = gBS->HandleProtocol(Handle, &gEfiDevicePathProtocolGuid, (VOID **)&Path);
  if (EFI_ERROR(Status))
    return Status;
  if (Path == NULL || !IsDevicePathValid(Path, 0))
    return EFI_VOLUME_CORRUPTED;
  for (Node = Path; !IsDevicePathEnd(Node); Node = NextDevicePathNode(Node))
  {
    if (IsDevicePathEndType(Node))
      return EFI_UNSUPPORTED;
    if (DevicePathType(Node) != MEDIA_DEVICE_PATH || DevicePathSubType(Node) != MEDIA_HARDDRIVE_DP)
      continue;
    if (Node == Path || DevicePathNodeLength(Node) != sizeof(*HardDrive) ||
        !IsDevicePathEnd(NextDevicePathNode(Node)))
      return EFI_UNSUPPORTED;
    HardDrive = (HARDDRIVE_DEVICE_PATH *)Node;
    if (HardDrive->MBRType != MBR_TYPE_EFI_PARTITION_TABLE_HEADER ||
        HardDrive->SignatureType != SIGNATURE_TYPE_GUID)
      return EFI_UNSUPPORTED;
    CopyMem(Type, &Partition->Info.Gpt.PartitionTypeGUID, sizeof(*Type));
    *DiskPath = Path;
    *DiskPathSize = (UINTN)((UINT8 *)Node - (UINT8 *)Path);
    return EFI_SUCCESS;
  }
  return EFI_UNSUPPORTED;
}

EFI_STATUS
InternalOpenBootVolume(CONST EFI_DEVICE_PATH_PROTOCOL *DevicePath, UINT16 Role,
                       OUT EFI_FILE_PROTOCOL **Root, OUT EFI_HANDLE *Handle)
{
  EFI_DEVICE_PATH_PROTOCOL        *Remaining;
  EFI_DEVICE_PATH_PROTOCOL        *Actual;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
  AC_APFS_VOLUME_INFO_PROTOCOL    *Identity;
  EFI_STATUS                       Status;
  UINTN                            Size;

  *Root = NULL;
  *Handle = NULL;
  if (DevicePath == NULL || !IsDevicePathValid(DevicePath, 0))
    return EFI_INVALID_PARAMETER;
  Remaining = (EFI_DEVICE_PATH_PROTOCOL *)DevicePath;
  Status = gBS->LocateDevicePath(&gEfiSimpleFileSystemProtocolGuid, &Remaining, Handle);
  if (EFI_ERROR(Status))
    return Status;
  if (!IsDevicePathEnd(Remaining))
    return EFI_NOT_FOUND;
  Status = gBS->HandleProtocol(*Handle, &gEfiDevicePathProtocolGuid, (VOID **)&Actual);
  if (EFI_ERROR(Status))
    return Status;
  Size = GetDevicePathSize(DevicePath);
  if (Actual == NULL || !IsDevicePathValid(Actual, 0) || GetDevicePathSize(Actual) != Size ||
      CompareMem(Actual, DevicePath, Size) != 0)
    return EFI_NOT_FOUND;
  Status = gBS->HandleProtocol(*Handle, &gAcApfsVolumeInfoProtocolGuid, (VOID **)&Identity);
  if (EFI_ERROR(Status))
    return Status;
  if (Identity->Revision != AC_APFS_VOLUME_INFO_PROTOCOL_REVISION || Identity->Role != Role)
    return EFI_UNSUPPORTED;
  Status = gBS->HandleProtocol(*Handle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&FileSystem);
  if (EFI_ERROR(Status))
    return Status;
  return FileSystem->OpenVolume(FileSystem, Root);
}

EFI_STATUS
InternalOpenBootRoot(CONST AC_BOOT_ENTRY *Entry, OUT EFI_FILE_PROTOCOL **Root,
                     OUT EFI_HANDLE *Handle)
{
  AC_APFS_VOLUME_INFO_PROTOCOL *Identity;
  EFI_STATUS                    Status;
  UINT16                        Role;

  *Root = NULL;
  *Handle = NULL;
  if (Entry->Type == AcBootEntryMacOS)
    Role = APFS_VOLUME_ROLE_PREBOOT;
  else if (Entry->Type == AcBootEntryRecovery)
    Role = APFS_VOLUME_ROLE_RECOVERY;
  else
    return EFI_UNSUPPORTED;
  Status = InternalOpenBootVolume(Entry->DevicePath, Role, Root, Handle);
  if (EFI_ERROR(Status))
    return Status;
  Status = gBS->HandleProtocol(*Handle, &gAcApfsVolumeInfoProtocolGuid, (VOID **)&Identity);
  if (!EFI_ERROR(Status) && (CompareMem(Identity->ContainerUuid, Entry->ContainerUuid, 16) != 0 ||
                             CompareMem(Identity->VolumeUuid, Entry->VolumeUuid, 16) != 0))
    Status = EFI_MEDIA_CHANGED;
  if (EFI_ERROR(Status))
  {
    (*Root)->Close(*Root);
    *Root = NULL;
  }
  return Status;
}

EFI_STATUS
InternalGetBootFileInfo(EFI_FILE_PROTOCOL *File, OUT EFI_FILE_INFO **Info)
{
  UINTN      Size = 0;
  UINTN      Capacity;
  EFI_STATUS Status;

  *Info = NULL;
  Status = File->GetInfo(File, &gEfiFileInfoGuid, &Size, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL)
    return EFI_ERROR(Status) ? Status : EFI_VOLUME_CORRUPTED;
  if (Size < SIZE_OF_EFI_FILE_INFO + sizeof(CHAR16))
    return EFI_VOLUME_CORRUPTED;
  Capacity = Size;
  *Info = AllocatePool(Size);
  if (*Info == NULL)
    return EFI_OUT_OF_RESOURCES;
  Status = File->GetInfo(File, &gEfiFileInfoGuid, &Size, *Info);
  if (!EFI_ERROR(Status) && (Size > Capacity || !ValidFileInfo(*Info, Size)))
    Status = EFI_VOLUME_CORRUPTED;
  if (EFI_ERROR(Status))
  {
    FreePool(*Info);
    *Info = NULL;
  }
  return Status;
}

EFI_STATUS
InternalReadBootDirectory(EFI_FILE_PROTOCOL *Directory, OUT EFI_FILE_INFO **Info)
{
  UINTN      Capacity = SIZE_OF_EFI_FILE_INFO + 256 * sizeof(CHAR16);
  UINTN      Size;
  EFI_STATUS Status;

  *Info = NULL;
  for (;;)
  {
    *Info = AllocatePool(Capacity);
    if (*Info == NULL)
      return EFI_OUT_OF_RESOURCES;
    Size = Capacity;
    Status = Directory->Read(Directory, &Size, *Info);
    if (Status != EFI_BUFFER_TOO_SMALL)
      break;
    FreePool(*Info);
    *Info = NULL;
    if (Size <= Capacity)
      return EFI_VOLUME_CORRUPTED;
    Capacity = Size;
  }
  if (!EFI_ERROR(Status) && Size != 0 && (Size > Capacity || !ValidFileInfo(*Info, Size)))
    Status = EFI_VOLUME_CORRUPTED;
  if (EFI_ERROR(Status) || Size == 0)
  {
    FreePool(*Info);
    *Info = NULL;
  }
  return Status;
}

EFI_STATUS
InternalReadBootFile(EFI_FILE_PROTOCOL *Root, CONST CHAR16 *Path, UINTN Limit, OUT VOID **Buffer,
                     OUT UINTN *Size)
{
  EFI_FILE_PROTOCOL *File;
  EFI_FILE_INFO     *Info = NULL;
  UINT8             *Data = NULL;
  UINTN              Length;
  UINTN              Offset;
  UINTN              Read;
  EFI_STATUS         Status;

  *Buffer = NULL;
  *Size = 0;
  Status = Root->Open(Root, &File, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(Status))
    return Status;
  Status = InternalGetBootFileInfo(File, &Info);
  if (EFI_ERROR(Status))
    goto Done;
  if ((Info->Attribute & EFI_FILE_DIRECTORY) != 0 || Info->FileSize > Limit)
  {
    Status = EFI_VOLUME_CORRUPTED;
    goto Done;
  }
  Length = (UINTN)Info->FileSize;
  Data = AllocatePool(Length == 0 ? 1 : Length);
  if (Data == NULL)
  {
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }
  for (Offset = 0; Offset < Length; Offset += Read)
  {
    Read = Length - Offset;
    Status = File->Read(File, &Read, Data + Offset);
    if (EFI_ERROR(Status))
      goto Done;
    if (Read == 0 || Read > Length - Offset)
    {
      Status = EFI_DEVICE_ERROR;
      goto Done;
    }
  }
  *Buffer = Data;
  *Size = Length;
  Data = NULL;

Done:
  if (Data != NULL)
    FreePool(Data);
  if (Info != NULL)
    FreePool(Info);
  File->Close(File);
  return Status;
}

CHAR16 *
InternalJoinBootPath(CONST CHAR16 *Directory, CONST CHAR16 *Name)
{
  UINTN   Base = StrLen(Directory);
  UINTN   Length = StrLen(Name);
  CHAR16 *Path;

  if (Base > MAX_UINTN / sizeof(CHAR16) - 2 || Length > MAX_UINTN / sizeof(CHAR16) - Base - 2)
    return NULL;
  Path = AllocatePool((Base + Length + 2) * sizeof(CHAR16));
  if (Path == NULL)
    return NULL;
  CopyMem(Path, Directory, Base * sizeof(CHAR16));
  if (Base != 0 && Path[Base - 1] != L'\\')
    Path[Base++] = L'\\';
  CopyMem(Path + Base, Name, (Length + 1) * sizeof(CHAR16));
  return Path;
}
