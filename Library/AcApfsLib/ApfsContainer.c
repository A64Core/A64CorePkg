/** @file
  Container lifetime and checkpoint selection.
**/

#include "ApfsInternal.h"

STATIC BOOLEAN
BlockRangeValid(CONST APFS_NX_SUPERBLOCK *Nx, UINT64 Base, UINT32 Count)
{
  return (Base != 0) && (Base < Nx->BlockCount) && (Count != 0) && (Count <= Nx->BlockCount - Base);
}

STATIC EFI_STATUS
CheckSuperblock(CONST AC_APFS_IO *Io, CONST APFS_NX_SUPERBLOCK *Nx)
{
  if ((Nx->Magic != APFS_NX_MAGIC) || (Nx->Object.Oid != 1) ||
      (Nx->Object.Type != (APFS_OBJECT_EPHEMERAL | APFS_OBJECT_NX_SUPERBLOCK)) ||
      (Nx->Object.Subtype != 0) || (Nx->Object.Xid == 0) || (Nx->BlockSize < APFS_MIN_BLOCK_SIZE) ||
      (Nx->BlockSize > APFS_MAX_BLOCK_SIZE) || ((Nx->BlockSize & (Nx->BlockSize - 1)) != 0) ||
      (Nx->BlockCount == 0) || (Nx->BlockCount > Io->Size / Nx->BlockSize) ||
      (Nx->MaxFileSystems > APFS_MAX_VOLUMES))
    return EFI_COMPROMISED_DATA;

  if (((Nx->DescriptorBlocks | Nx->DataBlocks) & APFS_XP_NONCONTIGUOUS) != 0)
    return EFI_UNSUPPORTED;
  if (!BlockRangeValid(Nx, Nx->DescriptorBase, Nx->DescriptorBlocks) ||
      !BlockRangeValid(Nx, Nx->DataBase, Nx->DataBlocks) ||
      ((Nx->DescriptorBase < Nx->DataBase + Nx->DataBlocks) &&
       (Nx->DataBase < Nx->DescriptorBase + Nx->DescriptorBlocks)))
    return EFI_COMPROMISED_DATA;

  return EFI_SUCCESS;
}

STATIC EFI_STATUS
CheckCheckpoint(CONST AC_APFS_CONTAINER *Container, UINT64 Address, BOOLEAN *ReadFailed)
{
  CONST APFS_NX_SUPERBLOCK *Nx;
  APFS_CHECKPOINT_MAP      *Map;
  APFS_CHECKPOINT_MAPPING  *Mappings;
  APFS_CHECKPOINT_MAPPING   Mapping;
  APFS_OBJECT               Object;
  EFI_STATUS                Status;
  VOID                     *Buffer;
  VOID                     *Payload;
  UINT64                    Block;
  UINT64                    Relative;
  UINT32                    Count;
  UINT32                    Capacity;
  UINT32                    Index;
  UINT32                    Entry;
  UINT32                    Previous;
  UINT32                    Blocks;
  BOOLEAN                   HasSpaceman;
  BOOLEAN                   HasReaper;

  Nx = &Container->Superblock;
  *ReadFailed = FALSE;
  if ((Nx->DescriptorLength < 2) || (Nx->DescriptorLength > Nx->DescriptorBlocks) ||
      (Nx->DescriptorIndex >= Nx->DescriptorBlocks) ||
      (Nx->DescriptorNext !=
       ((UINT64)Nx->DescriptorIndex + Nx->DescriptorLength) % Nx->DescriptorBlocks) ||
      (Nx->DataLength > Nx->DataBlocks) || (Nx->DataIndex >= Nx->DataBlocks) ||
      (Nx->DataNext != ((UINT64)Nx->DataIndex + Nx->DataLength) % Nx->DataBlocks) ||
      (Nx->DescriptorBase +
         ((UINT64)Nx->DescriptorIndex + Nx->DescriptorLength - 1) % Nx->DescriptorBlocks !=
       Address) ||
      (Nx->SpacemanOid == 0) || (Nx->ReaperOid == 0))
    return EFI_COMPROMISED_DATA;

  Capacity = Nx->DataLength;
  if (Capacity == 0)
    return EFI_COMPROMISED_DATA;
  Mappings = AllocateZeroPool((UINTN)Capacity * sizeof(*Mappings));
  if (Mappings == NULL)
    return EFI_OUT_OF_RESOURCES;

  Count = 0;
  HasSpaceman = FALSE;
  HasReaper = FALSE;
  Buffer = NULL;
  Payload = NULL;
  for (Index = 0; Index < Nx->DescriptorLength - 1; ++Index)
  {
    Block = Nx->DescriptorBase + ((UINT64)Nx->DescriptorIndex + Index) % Nx->DescriptorBlocks;
    Status = ApfsReadObject(Container, Block, Nx->BlockSize, &Buffer, ReadFailed);
    if (EFI_ERROR(Status))
      goto Done;

    Map = Buffer;
    Status = EFI_COMPROMISED_DATA;
    if ((Map->Object.Oid != Block) || (Map->Object.Xid != Nx->Object.Xid) ||
        (Map->Object.Type != (APFS_OBJECT_PHYSICAL | APFS_OBJECT_CHECKPOINT_MAP)) ||
        (Map->Object.Subtype != 0) ||
        (Map->Flags != ((Index == Nx->DescriptorLength - 2) ? APFS_CHECKPOINT_MAP_LAST : 0)) ||
        (Map->Count > (Nx->BlockSize - sizeof(*Map)) / sizeof(Mapping)) ||
        (Map->Count > Capacity - Count))
      goto Done;

    for (Entry = 0; Entry < Map->Count; ++Entry)
    {
      CopyMem(&Mapping, (UINT8 *)Buffer + sizeof(*Map) + Entry * sizeof(Mapping), sizeof(Mapping));
      if ((Mapping.Oid == 0) || (Mapping.Size == 0) || ((Mapping.Size % Nx->BlockSize) != 0) ||
          ((Mapping.Type & ~APFS_OBJECT_TYPE_MASK) != APFS_OBJECT_EPHEMERAL) ||
          (Mapping.Address < Nx->DataBase) || (Mapping.Address >= Nx->DataBase + Nx->DataBlocks))
        goto Done;

      Blocks = Mapping.Size / Nx->BlockSize;
      Relative = (Mapping.Address - Nx->DataBase + Nx->DataBlocks - Nx->DataIndex) % Nx->DataBlocks;
      if ((Blocks > Nx->DataBase + Nx->DataBlocks - Mapping.Address) ||
          (Relative >= Nx->DataLength) || (Blocks > Nx->DataLength - Relative))
        goto Done;
      for (Previous = 0; Previous < Count; ++Previous)
      {
        if (((Mappings[Previous].Oid == Mapping.Oid) &&
             (Mappings[Previous].FileSystemOid == Mapping.FileSystemOid)) ||
            ((Mapping.Address <
              Mappings[Previous].Address + Mappings[Previous].Size / Nx->BlockSize) &&
             (Mappings[Previous].Address < Mapping.Address + Blocks)))
          goto Done;
      }

      Status = ApfsReadObject(Container, Mapping.Address, Mapping.Size, &Payload, ReadFailed);
      if (EFI_ERROR(Status))
        goto Done;
      CopyMem(&Object, Payload, sizeof(Object));
      FreePool(Payload);
      Payload = NULL;
      Status = EFI_COMPROMISED_DATA;
      if ((Object.Oid != Mapping.Oid) || (Object.Xid > Nx->Object.Xid) ||
          (Object.Type != Mapping.Type) || (Object.Subtype != Mapping.Subtype))
        goto Done;

      if ((Mapping.FileSystemOid == 0) && (Mapping.Oid == Nx->SpacemanOid))
      {
        if (Mapping.Type != (APFS_OBJECT_EPHEMERAL | APFS_OBJECT_SPACEMAN))
          goto Done;
        HasSpaceman = TRUE;
      }
      if ((Mapping.FileSystemOid == 0) && (Mapping.Oid == Nx->ReaperOid))
      {
        if (Mapping.Type != (APFS_OBJECT_EPHEMERAL | APFS_OBJECT_REAPER))
          goto Done;
        HasReaper = TRUE;
      }
      Mappings[Count++] = Mapping;
    }
    FreePool(Buffer);
    Buffer = NULL;
  }
  Status = (HasSpaceman && HasReaper) ? EFI_SUCCESS : EFI_COMPROMISED_DATA;

Done:
  if (Payload != NULL)
    FreePool(Payload);
  if (Buffer != NULL)
    FreePool(Buffer);
  FreePool(Mappings);
  return Status;
}

EFI_STATUS
AcApfsOpenContainer(IN CONST AC_APFS_IO *Io, OUT AC_APFS_CONTAINER **Container)
{
  AC_APFS_CONTAINER *Result;
  APFS_NX_SUPERBLOCK Seed;
  APFS_NX_SUPERBLOCK Candidate;
  APFS_NX_SUPERBLOCK Best;
  EFI_STATUS         Status;
  EFI_STATUS         BestStatus;
  VOID              *Buffer;
  UINT32             Index;
  UINT32             Previous;
  UINT64             Address;
  BOOLEAN            ReadFailed;

  if (Container == NULL)
    return EFI_INVALID_PARAMETER;
  *Container = NULL;
  if ((Io == NULL) || (Io->Read == NULL))
    return EFI_INVALID_PARAMETER;
  if (Io->Size < sizeof(Seed))
    return EFI_UNSUPPORTED;

  Status = Io->Read(Io->Context, 0, sizeof(Seed), &Seed);
  if (EFI_ERROR(Status))
    return Status;
  if (Seed.Magic != APFS_NX_MAGIC)
    return EFI_UNSUPPORTED;
  Status = CheckSuperblock(Io, &Seed);
  if (EFI_ERROR(Status))
    return Status;

  Result = AllocateZeroPool(sizeof(*Result));
  if (Result == NULL)
    return EFI_OUT_OF_RESOURCES;
  Result->Io = *Io;
  Result->Superblock = Seed;
  Status = ApfsReadObject(Result, 0, Seed.BlockSize, &Buffer, NULL);
  if (EFI_ERROR(Status))
    goto Failure;
  FreePool(Buffer);

  ZeroMem(&Best, sizeof(Best));
  BestStatus = EFI_COMPROMISED_DATA;
  for (Index = 0; Index < Seed.DescriptorBlocks; ++Index)
  {
    Result->Superblock = Seed;
    Address = Seed.DescriptorBase + Index;
    Status = ApfsReadObject(Result, Address, Seed.BlockSize, &Buffer, &ReadFailed);
    if (ReadFailed)
      goto Failure;
    if (Status == EFI_COMPROMISED_DATA)
      continue;
    if (EFI_ERROR(Status))
      goto Failure;
    CopyMem(&Candidate, Buffer, sizeof(Candidate));
    FreePool(Buffer);
    if ((Candidate.Magic != APFS_NX_MAGIC) || (Candidate.Object.Xid < Best.Object.Xid))
      continue;
    Status = CheckSuperblock(Io, &Candidate);
    if (Status == EFI_COMPROMISED_DATA)
      continue;
    if ((Candidate.BlockSize != Seed.BlockSize) ||
        (CompareMem(Candidate.Uuid, Seed.Uuid, sizeof(Seed.Uuid)) != 0))
      continue;

    Result->Superblock = Candidate;
    if (!EFI_ERROR(Status))
      Status = CheckCheckpoint(Result, Address, &ReadFailed);
    if (ReadFailed)
      goto Failure;
    if (Status == EFI_COMPROMISED_DATA)
      continue;
    if (EFI_ERROR(Status) && (Status != EFI_UNSUPPORTED))
      goto Failure;
    if ((Candidate.Object.Xid == Best.Object.Xid) &&
        (CompareMem(&Candidate, &Best, sizeof(Best)) != 0))
    {
      Status = EFI_COMPROMISED_DATA;
      goto Failure;
    }
    Best = Candidate;
    BestStatus = Status;
  }
  Status = BestStatus;
  if (EFI_ERROR(Status))
    goto Failure;
  Result->Superblock = Best;
  if (Best.IncompatibleFeatures != APFS_NX_INCOMPAT_VERSION2)
  {
    Status = EFI_UNSUPPORTED;
    goto Failure;
  }
  for (Index = 0; Index < APFS_MAX_VOLUMES; ++Index)
  {
    if (Best.FileSystemOid[Index] == 0)
      continue;
    if (Index >= Best.MaxFileSystems)
    {
      Status = EFI_COMPROMISED_DATA;
      goto Failure;
    }
    for (Previous = 0; Previous < Index; ++Previous)
    {
      if (Best.FileSystemOid[Previous] == Best.FileSystemOid[Index])
      {
        Status = EFI_COMPROMISED_DATA;
        goto Failure;
      }
    }
    ++Result->VolumeCount;
  }
  Status = ApfsOpenObjectMap(Result, Best.OmapOid, TRUE, &Result->OmapRoot);
  if (EFI_ERROR(Status))
    goto Failure;
  *Container = Result;
  return EFI_SUCCESS;

Failure:
  AcApfsCloseContainer(Result);
  return Status;
}

VOID
AcApfsCloseContainer(IN AC_APFS_CONTAINER *Container OPTIONAL)
{
  if (Container != NULL)
    FreePool(Container);
}

EFI_STATUS
AcApfsGetContainerInfo(IN CONST AC_APFS_CONTAINER *Container, OUT AC_APFS_CONTAINER_INFO *Info)
{
  if (Info == NULL)
    return EFI_INVALID_PARAMETER;
  ZeroMem(Info, sizeof(*Info));
  if (Container == NULL)
    return EFI_INVALID_PARAMETER;
  CopyMem(Info->Uuid, Container->Superblock.Uuid, sizeof(Info->Uuid));
  Info->BlockSize = Container->Superblock.BlockSize;
  Info->BlockCount = Container->Superblock.BlockCount;
  Info->CheckpointXid = Container->Superblock.Object.Xid;
  Info->VolumeCount = Container->VolumeCount;
  return EFI_SUCCESS;
}
