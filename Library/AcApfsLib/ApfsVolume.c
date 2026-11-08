/** @file
  Volume superblock metadata enumeration.
**/

#include "ApfsInternal.h"

EFI_STATUS
ApfsReadVolumeSuperblock(CONST AC_APFS_CONTAINER *Container, UINT32 Index,
                         OUT APFS_VOLUME_SUPERBLOCK *Superblock)
{
  APFS_VOLUME_SUPERBLOCK Volume;
  APFS_OMAP_KEY          Key;
  APFS_OMAP_VALUE        Value;
  VOID                  *Buffer;
  EFI_STATUS             Status;
  UINT32                 Slot;
  UINT32                 Dense;
  UINT32                 Length;
  UINT64                 Oid;

  ZeroMem(Superblock, sizeof(*Superblock));
  if (Container == NULL)
    return EFI_INVALID_PARAMETER;
  if (Index >= Container->VolumeCount)
    return EFI_NOT_FOUND;

  Oid = 0;
  Dense = 0;
  for (Slot = 0; Slot < Container->Superblock.MaxFileSystems; ++Slot)
  {
    Oid = Container->Superblock.FileSystemOid[Slot];
    if ((Oid != 0) && (Dense++ == Index))
      break;
  }
  Status = ApfsLookupObject(Container, Container->OmapRoot, Container->Superblock.Object.Xid, Oid,
                            &Key, &Value);
  if (EFI_ERROR(Status))
    return Status == EFI_NOT_FOUND ? EFI_COMPROMISED_DATA : Status;
  Status = ApfsReadObject(Container, Value.Address, Value.Size, &Buffer, NULL);
  if (EFI_ERROR(Status))
    return Status;
  CopyMem(&Volume, Buffer, sizeof(Volume));
  FreePool(Buffer);
  if ((Volume.Magic != APFS_VOLUME_MAGIC) || (Volume.Object.Oid != Oid) ||
      (Volume.Object.Xid != Key.Xid) || (Volume.Object.Type != APFS_OBJECT_VOLUME) ||
      (Volume.Object.Subtype != 0) || (Volume.FileSystemIndex != Slot))
    return EFI_COMPROMISED_DATA;

  for (Length = 0; Length < sizeof(Volume.Name); ++Length)
    if (Volume.Name[Length] == '\0')
      break;
  if (Length == sizeof(Volume.Name))
    return EFI_COMPROMISED_DATA;

  *Superblock = Volume;
  return EFI_SUCCESS;
}

EFI_STATUS
AcApfsGetVolumeInfo(IN CONST AC_APFS_CONTAINER *Container, IN UINT32 Index,
                    OUT AC_APFS_VOLUME_INFO *Info)
{
  APFS_VOLUME_SUPERBLOCK Volume;
  EFI_STATUS             Status;

  if (Info == NULL)
    return EFI_INVALID_PARAMETER;
  ZeroMem(Info, sizeof(*Info));
  Status = ApfsReadVolumeSuperblock(Container, Index, &Volume);
  if (EFI_ERROR(Status))
    return Status;
  CopyMem(Info->Uuid, Volume.Uuid, sizeof(Info->Uuid));
  CopyMem(Info->GroupUuid, Volume.GroupUuid, sizeof(Info->GroupUuid));
  Info->Role = Volume.Role;
  Info->Features = Volume.Features;
  Info->ReadOnlyCompatibleFeatures = Volume.ReadOnlyCompatibleFeatures;
  Info->IncompatibleFeatures = Volume.IncompatibleFeatures;
  Info->Flags = Volume.Flags;
  CopyMem(Info->Name, Volume.Name, sizeof(Info->Name));
  return EFI_SUCCESS;
}

EFI_STATUS
AcApfsOpenVolume(IN CONST AC_APFS_CONTAINER *Container, IN UINT32 Index,
                 OUT AC_APFS_VOLUME **Volume)
{
  AC_APFS_VOLUME        *Result;
  APFS_VOLUME_SUPERBLOCK Superblock;
  APFS_INODE_INFO        Root;
  EFI_STATUS             Status;

  if (Volume == NULL)
    return EFI_INVALID_PARAMETER;
  *Volume = NULL;
  Status = ApfsReadVolumeSuperblock(Container, Index, &Superblock);
  if (EFI_ERROR(Status))
    return Status;
  if ((Superblock.Flags & (APFS_FS_UNENCRYPTED | APFS_FS_ONEKEY)) != APFS_FS_UNENCRYPTED ||
      (Superblock.IncompatibleFeatures &
       ~(APFS_INCOMPAT_CASE_INSENSITIVE | APFS_INCOMPAT_NORMALIZATION_INSENSITIVE |
         APFS_INCOMPAT_DATALESS_SNAPS)) != 0 ||
      Superblock.RevertToXid != 0 || Superblock.RevertToSuperblockOid != 0 ||
      Superblock.RootToXid != 0 || Superblock.EncryptionStateOid != 0)
    return EFI_UNSUPPORTED;
  if (Superblock.RootTreeType != APFS_OBJECT_BTREE)
    return EFI_UNSUPPORTED;
  Result = AllocateZeroPool(sizeof(*Result));
  if (Result == NULL)
    return EFI_OUT_OF_RESOURCES;
  Result->Container = Container;
  Result->IncompatibleFeatures = Superblock.IncompatibleFeatures;
  Result->Tree.Container = Container;
  Result->Tree.Root = Superblock.RootTreeOid;
  Result->Tree.Xid = Container->Superblock.Object.Xid;
  Result->Tree.Subtype = APFS_OBJECT_FSTREE;
  Result->Tree.HashedNames =
    (Superblock.IncompatibleFeatures &
     (APFS_INCOMPAT_CASE_INSENSITIVE | APFS_INCOMPAT_NORMALIZATION_INSENSITIVE)) != 0;
  Result->RootId = APFS_ROOT_INODE;
  Status = ApfsOpenObjectMap(Container, Superblock.OmapOid, FALSE, &Result->Tree.OmapRoot);
  if (!EFI_ERROR(Status))
    Status = ApfsBtreeCheckRoot(&Result->Tree);
  if (!EFI_ERROR(Status))
    Status = ApfsReadInode(Result, Result->RootId, &Root);
  if (!EFI_ERROR(Status) && Root.Info.Type != AcApfsFileDirectory)
    Status = EFI_COMPROMISED_DATA;
  if (EFI_ERROR(Status))
  {
    AcApfsCloseVolume(Result);
    return Status;
  }
  *Volume = Result;
  return EFI_SUCCESS;
}

VOID
AcApfsCloseVolume(IN AC_APFS_VOLUME *Volume OPTIONAL)
{
  if (Volume != NULL)
    FreePool(Volume);
}
