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
