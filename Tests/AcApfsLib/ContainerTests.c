/** @file
  Synthetic APFS partition tests using public library interfaces.
**/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Library/AcApfsLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

STATIC UINTN OutstandingPools;
STATIC UINTN AllocationCalls;
STATIC UINTN FailAllocationAt;

VOID *EFIAPI
CopyMem(VOID *Destination, CONST VOID *Source, UINTN Length)
{
  return memmove(Destination, Source, Length);
}

VOID *EFIAPI
ZeroMem(VOID *Buffer, UINTN Length)
{
  return memset(Buffer, 0, Length);
}

INTN EFIAPI
CompareMem(CONST VOID *First, CONST VOID *Second, UINTN Length)
{
  return memcmp(First, Second, Length);
}

VOID *EFIAPI
AllocateZeroPool(UINTN Size)
{
  VOID *Buffer;

  ++AllocationCalls;
  if ((FailAllocationAt != 0) && (AllocationCalls == FailAllocationAt))
    return NULL;
  Buffer = calloc(1, Size);
  if (Buffer != NULL)
    ++OutstandingPools;
  return Buffer;
}

VOID EFIAPI
FreePool(VOID *Buffer)
{
  assert((Buffer != NULL) && (OutstandingPools != 0));
  --OutstandingPools;
  free(Buffer);
}

enum
{
  BlockSize = 4096,
  BlockCount = 256,
  DiskSize = BlockSize * BlockCount,
  OldMap = 2,
  OldNx = 3,
  NewMap = 6,
  NewNx = 8,
  OmapBlock = 32,
  RootBlock = 33,
  LeftBlock = 34,
  RightBlock = 35,
  PrebootBlock = 40,
  RecoveryBlock = 41,
  SystemBlock = 42,
  FutureBlock = 43
};

typedef struct
{
  UINT8     *Storage;
  UINT8     *Disk;
  AC_APFS_IO Io;
  UINTN      ReadCalls;
  UINTN      FailReadAt;
  EFI_STATUS FailReadStatus;
} FIXTURE;

STATIC VOID
Put16(UINT8 *Buffer, UINTN Offset, UINT16 Value)
{
  CopyMem(Buffer + Offset, &Value, sizeof(Value));
}

STATIC VOID
Put32(UINT8 *Buffer, UINTN Offset, UINT32 Value)
{
  CopyMem(Buffer + Offset, &Value, sizeof(Value));
}

STATIC VOID
Put64(UINT8 *Buffer, UINTN Offset, UINT64 Value)
{
  CopyMem(Buffer + Offset, &Value, sizeof(Value));
}

STATIC UINT8 *
Block(FIXTURE *Fixture, UINT32 Number)
{
  assert(Number < BlockCount);
  return Fixture->Disk + Number * BlockSize;
}

STATIC VOID
Checksum(UINT8 *Buffer, UINT32 Size)
{
  UINT64 First;
  UINT64 Second;
  UINT64 Low;
  UINT64 High;
  UINT32 Word;
  UINT32 Offset;

  First = 0;
  Second = 0;
  for (Offset = 8; Offset < Size; Offset += 4)
  {
    CopyMem(&Word, Buffer + Offset, sizeof(Word));
    First += Word;
    Second += First;
    First %= 0xFFFFFFFFULL;
    Second %= 0xFFFFFFFFULL;
  }
  Low = 0xFFFFFFFFULL - ((First + Second) % 0xFFFFFFFFULL);
  High = 0xFFFFFFFFULL - ((First + Low) % 0xFFFFFFFFULL);
  Put64(Buffer, 0, Low | (High << 32));
}

STATIC VOID
Seal(FIXTURE *Fixture, UINT32 Number)
{
  Checksum(Block(Fixture, Number), BlockSize);
}

STATIC VOID
Object(UINT8 *Buffer, UINT64 Oid, UINT64 Xid, UINT32 Type, UINT32 Subtype)
{
  Put64(Buffer, 8, Oid);
  Put64(Buffer, 16, Xid);
  Put32(Buffer, 24, Type);
  Put32(Buffer, 28, Subtype);
}

STATIC EFI_STATUS EFIAPI
ReadDisk(VOID *Context, UINT64 Offset, UINTN Size, VOID *Buffer)
{
  FIXTURE *Fixture;

  Fixture = Context;
  ++Fixture->ReadCalls;
  if (Fixture->ReadCalls == Fixture->FailReadAt)
    return Fixture->FailReadStatus;
  assert(Offset <= Fixture->Io.Size);
  assert(Size <= Fixture->Io.Size - Offset);
  CopyMem(Buffer, Fixture->Disk + (UINTN)Offset, Size);
  return EFI_SUCCESS;
}

STATIC VOID
WriteNx(FIXTURE *Fixture, UINT32 Number, UINT64 Xid, UINT32 FirstMap, UINT32 Length,
        UINT32 DataIndex)
{
  APFS_NX_SUPERBLOCK Nx;

  ZeroMem(&Nx, sizeof(Nx));
  Nx.Object.Oid = 1;
  Nx.Object.Xid = Xid;
  Nx.Object.Type = APFS_OBJECT_EPHEMERAL | APFS_OBJECT_NX_SUPERBLOCK;
  Nx.Magic = APFS_NX_MAGIC;
  Nx.BlockSize = BlockSize;
  Nx.BlockCount = BlockCount;
  Nx.IncompatibleFeatures = APFS_NX_INCOMPAT_VERSION2;
  Nx.Uuid[0] = 0xA1;
  Nx.NextOid = 3000;
  Nx.NextXid = Xid + 1;
  Nx.DescriptorBase = 1;
  Nx.DescriptorBlocks = 8;
  Nx.DataBase = 16;
  Nx.DataBlocks = 16;
  Nx.DescriptorIndex = FirstMap - 1;
  Nx.DescriptorLength = Length;
  Nx.DescriptorNext = (Nx.DescriptorIndex + Length) % Nx.DescriptorBlocks;
  Nx.DataIndex = DataIndex;
  Nx.DataLength = 2;
  Nx.DataNext = (DataIndex + 2) % Nx.DataBlocks;
  Nx.SpacemanOid = 1100;
  Nx.ReaperOid = 1101;
  Nx.OmapOid = OmapBlock;
  Nx.MaxFileSystems = 100;
  Nx.FileSystemOid[0] = 2000;
  Nx.FileSystemOid[3] = 2001;
  Nx.FileSystemOid[7] = 2002;
  CopyMem(Block(Fixture, Number), &Nx, sizeof(Nx));
  Seal(Fixture, Number);
}

STATIC VOID
WriteMap(FIXTURE *Fixture, UINT32 Number, UINT64 Xid, UINT32 FirstObject, UINT32 Count,
         BOOLEAN Last, UINT32 DataBlock)
{
  APFS_CHECKPOINT_MAP     Map;
  APFS_CHECKPOINT_MAPPING Mapping;
  UINT32                  Index;

  ZeroMem(&Map, sizeof(Map));
  Map.Object.Oid = Number;
  Map.Object.Xid = Xid;
  Map.Object.Type = APFS_OBJECT_PHYSICAL | APFS_OBJECT_CHECKPOINT_MAP;
  Map.Flags = Last ? APFS_CHECKPOINT_MAP_LAST : 0;
  Map.Count = Count;
  CopyMem(Block(Fixture, Number), &Map, sizeof(Map));
  for (Index = 0; Index < Count; ++Index)
  {
    ZeroMem(&Mapping, sizeof(Mapping));
    Mapping.Type = APFS_OBJECT_EPHEMERAL |
                   ((FirstObject + Index == 0) ? APFS_OBJECT_SPACEMAN : APFS_OBJECT_REAPER);
    Mapping.Size = BlockSize;
    Mapping.Oid = 1100 + FirstObject + Index;
    Mapping.Address = DataBlock + Index;
    CopyMem(Block(Fixture, Number) + sizeof(Map) + Index * sizeof(Mapping), &Mapping,
            sizeof(Mapping));
    Object(Block(Fixture, DataBlock + Index), Mapping.Oid, Xid, Mapping.Type, 0);
    Seal(Fixture, DataBlock + Index);
  }
  Seal(Fixture, Number);
}

STATIC VOID
WriteNode(FIXTURE *Fixture, UINT32 Number, BOOLEAN Root, UINT16 Level, CONST APFS_OMAP_KEY *Keys,
          CONST APFS_OMAP_VALUE *Values, CONST UINT64 *Children, UINT32 Count)
{
  APFS_BTREE_NODE Node;
  APFS_BTREE_INFO Info;
  APFS_KV_OFFSET  Offset;
  UINTN           KeyBase;
  UINTN           ValueEnd;
  UINTN           ValueSize;
  UINT32          Index;

  ZeroMem(&Node, sizeof(Node));
  Node.Object.Oid = Number;
  Node.Object.Xid = 10;
  Node.Object.Type = APFS_OBJECT_PHYSICAL | (Root ? APFS_OBJECT_BTREE : APFS_OBJECT_BTREE_NODE);
  Node.Object.Subtype = APFS_OBJECT_OMAP;
  Node.Flags =
    APFS_BTNODE_FIXED_KV | (Root ? APFS_BTNODE_ROOT : 0) | (Level == 0 ? APFS_BTNODE_LEAF : 0);
  Node.Level = Level;
  Node.KeyCount = Count;
  Node.TableSpace.Offset = 8;
  Node.TableSpace.Length = (UINT16)(Count * sizeof(Offset) + 16);
  KeyBase = sizeof(Node) + Node.TableSpace.Offset + Node.TableSpace.Length;
  ValueEnd = BlockSize - (Root ? sizeof(Info) : 0);
  ValueSize = Level == 0 ? sizeof(*Values) : sizeof(*Children);
  Node.FreeSpace.Offset = (UINT16)(Count * sizeof(*Keys));
  Node.FreeSpace.Length = (UINT16)(ValueEnd - Count * ValueSize - KeyBase - Node.FreeSpace.Offset);
  CopyMem(Block(Fixture, Number), &Node, sizeof(Node));
  for (Index = 0; Index < Count; ++Index)
  {
    Offset.KeyOffset = (UINT16)(Index * sizeof(*Keys));
    Offset.ValueOffset = (UINT16)((Index + 1) * ValueSize);
    CopyMem(Block(Fixture, Number) + sizeof(Node) + Node.TableSpace.Offset + Index * sizeof(Offset),
            &Offset, sizeof(Offset));
    CopyMem(Block(Fixture, Number) + KeyBase + Offset.KeyOffset, &Keys[Index], sizeof(*Keys));
    CopyMem(Block(Fixture, Number) + ValueEnd - Offset.ValueOffset,
            Level == 0 ? (CONST VOID *)&Values[Index] : (CONST VOID *)&Children[Index], ValueSize);
  }
  if (Root)
  {
    ZeroMem(&Info, sizeof(Info));
    Info.Flags = APFS_BTREE_PHYSICAL;
    Info.NodeSize = BlockSize;
    Info.KeySize = sizeof(*Keys);
    Info.ValueSize = sizeof(*Values);
    Info.LongestKey = sizeof(*Keys);
    Info.LongestValue = sizeof(*Values);
    Info.KeyCount = Level == 0 ? Count : 5;
    Info.NodeCount = Level == 0 ? 1 : 3;
    CopyMem(Block(Fixture, Number) + ValueEnd, &Info, sizeof(Info));
  }
  Seal(Fixture, Number);
}

STATIC VOID
WriteVolume(FIXTURE *Fixture, UINT32 Number, UINT64 Oid, UINT64 Xid, UINT32 Slot, UINT16 Role,
            CONST CHAR8 *Name)
{
  APFS_VOLUME_SUPERBLOCK Volume;

  ZeroMem(&Volume, sizeof(Volume));
  Volume.Object.Oid = Oid;
  Volume.Object.Xid = Xid;
  Volume.Object.Type = APFS_OBJECT_VOLUME;
  Volume.Magic = APFS_VOLUME_MAGIC;
  Volume.FileSystemIndex = Slot;
  Volume.Role = Role;
  Volume.Uuid[0] = (UINT8)Oid;
  Volume.GroupUuid[0] = 0xC1;
  Volume.Flags = 1;
  CopyMem(Volume.Name, Name, strlen(Name) + 1);
  CopyMem(Block(Fixture, Number), &Volume, sizeof(Volume));
  Seal(Fixture, Number);
}

STATIC VOID
Reset(FIXTURE *Fixture)
{
  APFS_OMAP             Omap;
  CONST APFS_OMAP_KEY   LeftKeys[] = {{1999, 1}, {2000, 5}, {2000, 11}};
  CONST APFS_OMAP_KEY   RightKeys[] = {{2001, 7}, {2002, 10}};
  CONST APFS_OMAP_KEY   RootKeys[] = {{1999, 1}, {2001, 7}};
  CONST APFS_OMAP_VALUE LeftValues[] = {
    {0, BlockSize, FutureBlock}, {0, BlockSize, PrebootBlock}, {0, BlockSize, FutureBlock}};
  CONST APFS_OMAP_VALUE RightValues[] = {{0, BlockSize, RecoveryBlock},
                                         {0, BlockSize, SystemBlock}};
  CONST UINT64          Children[] = {LeftBlock, RightBlock};

  assert(OutstandingPools == 0);
  ZeroMem(Fixture->Disk, DiskSize);
  Fixture->ReadCalls = 0;
  Fixture->FailReadAt = 0;
  Fixture->FailReadStatus = EFI_DEVICE_ERROR;
  Fixture->Io.Context = Fixture;
  Fixture->Io.Read = ReadDisk;
  Fixture->Io.Size = DiskSize;
  AllocationCalls = 0;
  FailAllocationAt = 0;
  WriteNx(Fixture, 0, 4, OldMap, 2, 0);
  WriteNx(Fixture, OldNx, 4, OldMap, 2, 0);
  WriteMap(Fixture, OldMap, 4, 0, 2, TRUE, 16);
  WriteNx(Fixture, NewNx, 10, NewMap, 3, 4);
  WriteMap(Fixture, NewMap, 10, 0, 1, FALSE, 20);
  WriteMap(Fixture, NewMap + 1, 10, 1, 1, TRUE, 21);
  ZeroMem(&Omap, sizeof(Omap));
  Omap.Object.Oid = OmapBlock;
  Omap.Object.Xid = 4;
  Omap.Object.Type = APFS_OBJECT_PHYSICAL | APFS_OBJECT_OMAP;
  Omap.Flags = APFS_OMAP_MANUALLY_MANAGED;
  Omap.TreeType = APFS_OBJECT_PHYSICAL | APFS_OBJECT_BTREE;
  Omap.TreeOid = RootBlock;
  CopyMem(Block(Fixture, OmapBlock), &Omap, sizeof(Omap));
  Seal(Fixture, OmapBlock);
  WriteNode(Fixture, RootBlock, TRUE, 1, RootKeys, NULL, Children, 2);
  WriteNode(Fixture, LeftBlock, FALSE, 0, LeftKeys, LeftValues, NULL, 3);
  WriteNode(Fixture, RightBlock, FALSE, 0, RightKeys, RightValues, NULL, 2);
  WriteVolume(Fixture, PrebootBlock, 2000, 5, 0, APFS_VOLUME_ROLE_PREBOOT, "Preboot");
  WriteVolume(Fixture, RecoveryBlock, 2001, 7, 3, APFS_VOLUME_ROLE_RECOVERY, "Recovery");
  WriteVolume(Fixture, SystemBlock, 2002, 10, 7, APFS_VOLUME_ROLE_SYSTEM, "System");
  Put64(Block(Fixture, SystemBlock), 0x38, 0x20);
  Seal(Fixture, SystemBlock);
  WriteVolume(Fixture, FutureBlock, 2000, 11, 0, APFS_VOLUME_ROLE_PREBOOT, "Future");
}

STATIC VOID
ExpectOpen(FIXTURE *Fixture, EFI_STATUS Expected, UINT64 Xid)
{
  AC_APFS_CONTAINER     *Container;
  AC_APFS_CONTAINER_INFO Info;
  EFI_STATUS             Status;

  Container = (VOID *)(UINTN)1;
  Status = AcApfsOpenContainer(&Fixture->Io, &Container);
  if (Status != Expected)
    fprintf(stderr, "Open: expected 0x%llx (XID %llu), got 0x%llx\n", (unsigned long long)Expected,
            (unsigned long long)Xid, (unsigned long long)Status);
  assert(Status == Expected);
  if (EFI_ERROR(Expected))
    assert(Container == NULL);
  else
  {
    assert(Container != NULL);
    assert(AcApfsGetContainerInfo(Container, &Info) == EFI_SUCCESS);
    assert(Info.CheckpointXid == Xid);
  }
  AcApfsCloseContainer(Container);
  assert(OutstandingPools == 0);
}

STATIC VOID
TestEnumeration(FIXTURE *Fixture)
{
  AC_APFS_CONTAINER     *Container;
  AC_APFS_CONTAINER_INFO Info;
  AC_APFS_VOLUME_INFO    Volume;
  UINT64                 KnownChecksum;

  Reset(Fixture);
  CopyMem(&KnownChecksum, Block(Fixture, 20), sizeof(KnownChecksum));
  assert(KnownChecksum == 0x0011651F7FEE9685ULL);
  assert(AcApfsOpenContainer(&Fixture->Io, &Container) == EFI_SUCCESS);
  assert(AcApfsGetContainerInfo(Container, &Info) == EFI_SUCCESS);
  assert(Info.VolumeCount == 3 && Info.BlockSize == BlockSize && Info.BlockCount == BlockCount);
  assert(Info.CheckpointXid == 10 && Info.Uuid[0] == 0xA1);
  assert(AcApfsGetVolumeInfo(Container, 0, &Volume) == EFI_SUCCESS);
  assert(strcmp(Volume.Name, "Preboot") == 0 && Volume.Role == APFS_VOLUME_ROLE_PREBOOT);
  assert(Volume.Uuid[0] == (UINT8)2000 && Volume.GroupUuid[0] == 0xC1);
  assert(AcApfsGetVolumeInfo(Container, 1, &Volume) == EFI_SUCCESS);
  assert(strcmp(Volume.Name, "Recovery") == 0 && Volume.Role == APFS_VOLUME_ROLE_RECOVERY);
  assert(AcApfsGetVolumeInfo(Container, 2, &Volume) == EFI_SUCCESS);
  assert(Volume.IncompatibleFeatures == 0x20 && strcmp(Volume.Name, "System") == 0);
  assert(AcApfsGetVolumeInfo(Container, 3, &Volume) == EFI_NOT_FOUND);
  assert(Volume.Name[0] == 0);
  assert(AcApfsGetVolumeInfo(NULL, 0, &Volume) == EFI_INVALID_PARAMETER);
  assert(AcApfsGetVolumeInfo(Container, 0, NULL) == EFI_INVALID_PARAMETER);
  assert(AcApfsGetContainerInfo(NULL, &Info) == EFI_INVALID_PARAMETER);
  assert(AcApfsGetContainerInfo(Container, NULL) == EFI_INVALID_PARAMETER);
  AcApfsCloseContainer(Container);
  AcApfsCloseContainer(NULL);
  assert(OutstandingPools == 0);
}

STATIC VOID
TestCheckpoints(FIXTURE *Fixture)
{
  Reset(Fixture);
  Block(Fixture, NewNx)[1000] ^= 1;
  ExpectOpen(Fixture, EFI_COMPROMISED_DATA, 0); // Old tree is newer than the old checkpoint.
  Reset(Fixture);
  Put64(Block(Fixture, RootBlock), 16, 4);
  Seal(Fixture, RootBlock);
  Block(Fixture, NewNx)[1000] ^= 1;
  ExpectOpen(Fixture, EFI_SUCCESS, 4);
  Reset(Fixture);
  Put64(Block(Fixture, RootBlock), 16, 4);
  Seal(Fixture, RootBlock);
  Block(Fixture, 21)[64] ^= 1;
  ExpectOpen(Fixture, EFI_SUCCESS, 4);
  Reset(Fixture);
  Put64(Block(Fixture, RootBlock), 16, 4);
  Seal(Fixture, RootBlock);
  Put32(Block(Fixture, NewMap + 1), 32, 0);
  Seal(Fixture, NewMap + 1);
  ExpectOpen(Fixture, EFI_SUCCESS, 4);
  Reset(Fixture);
  Put64(Block(Fixture, NewNx), 0x40, 0x102);
  Seal(Fixture, NewNx);
  ExpectOpen(Fixture, EFI_UNSUPPORTED, 0);
  Reset(Fixture);
  Put32(Block(Fixture, 0), 0x68, 0x80000008);
  Seal(Fixture, 0);
  ExpectOpen(Fixture, EFI_UNSUPPORTED, 0);
  Reset(Fixture);
  ZeroMem(Block(Fixture, OldNx), BlockSize);
  ZeroMem(Block(Fixture, NewNx), BlockSize);
  ExpectOpen(Fixture, EFI_COMPROMISED_DATA, 0);
  Reset(Fixture);
  WriteMap(Fixture, 8, 12, 0, 2, TRUE, 22);
  WriteNx(Fixture, 1, 12, 8, 2, 6);
  ExpectOpen(Fixture, EFI_SUCCESS, 12);
  Reset(Fixture);
  Put64(Block(Fixture, NewMap), 16, 9);
  Seal(Fixture, NewMap);
  Put64(Block(Fixture, RootBlock), 16, 4);
  Seal(Fixture, RootBlock);
  ExpectOpen(Fixture, EFI_SUCCESS, 4);
}

STATIC EFI_STATUS
ReadFirst(FIXTURE *Fixture)
{
  AC_APFS_CONTAINER  *Container;
  AC_APFS_VOLUME_INFO Info;
  EFI_STATUS          Status;

  Status = AcApfsOpenContainer(&Fixture->Io, &Container);
  if (!EFI_ERROR(Status))
  {
    Status = AcApfsGetVolumeInfo(Container, 0, &Info);
    AcApfsCloseContainer(Container);
  }
  assert(OutstandingPools == 0);
  return Status;
}

STATIC VOID
TestBadMetadata(FIXTURE *Fixture)
{
  UINTN Size;

  Reset(Fixture);
  assert(AcApfsOpenContainer(NULL, NULL) == EFI_INVALID_PARAMETER);
  ExpectOpen(Fixture, EFI_SUCCESS, 10);
  Reset(Fixture);
  Put32(Block(Fixture, NewNx), 0x24, 8192);
  Seal(Fixture, NewNx);
  Put64(Block(Fixture, RootBlock), 16, 4);
  Seal(Fixture, RootBlock);
  ExpectOpen(Fixture, EFI_SUCCESS, 4);
  Reset(Fixture);
  Put64(Block(Fixture, 0), 0x28, MAX_UINT64);
  Seal(Fixture, 0);
  ExpectOpen(Fixture, EFI_COMPROMISED_DATA, 0);
  Reset(Fixture);
  Put64(Block(Fixture, NewNx), 0xB8 + 8, 2000);
  Seal(Fixture, NewNx);
  ExpectOpen(Fixture, EFI_COMPROMISED_DATA, 0);
  Reset(Fixture);
  Put16(Block(Fixture, RootBlock), 40, 0xFFFF);
  Seal(Fixture, RootBlock);
  ExpectOpen(Fixture, EFI_COMPROMISED_DATA, 0);
  Reset(Fixture);
  Put64(Block(Fixture, RootBlock), BlockSize - sizeof(APFS_BTREE_INFO) - 8, RootBlock);
  Seal(Fixture, RootBlock);
  assert(ReadFirst(Fixture) == EFI_COMPROMISED_DATA);
  Reset(Fixture);
  Put16(Block(Fixture, LeftBlock), sizeof(APFS_BTREE_NODE) + 8 + 4 + 2, 0xFFFF);
  Seal(Fixture, LeftBlock);
  assert(ReadFirst(Fixture) == EFI_COMPROMISED_DATA);
  Reset(Fixture);
  Put32(Block(Fixture, LeftBlock), BlockSize - 32, APFS_OMAP_VAL_DELETED);
  Seal(Fixture, LeftBlock);
  assert(ReadFirst(Fixture) == EFI_COMPROMISED_DATA);
  Reset(Fixture);
  Put32(Block(Fixture, LeftBlock), BlockSize - 32, APFS_OMAP_VAL_ENCRYPTED);
  Seal(Fixture, LeftBlock);
  assert(ReadFirst(Fixture) == EFI_UNSUPPORTED);
  Reset(Fixture);
  Put64(Block(Fixture, LeftBlock), BlockSize - 32 + 8, MAX_UINT64);
  Seal(Fixture, LeftBlock);
  assert(ReadFirst(Fixture) == EFI_COMPROMISED_DATA);
  Reset(Fixture);
  memset(Block(Fixture, PrebootBlock) + 0x2C0, 'A', 256);
  Seal(Fixture, PrebootBlock);
  assert(ReadFirst(Fixture) == EFI_COMPROMISED_DATA);
  Reset(Fixture);
  Put64(Block(Fixture, PrebootBlock), 16, 4);
  Seal(Fixture, PrebootBlock);
  assert(ReadFirst(Fixture) == EFI_COMPROMISED_DATA);
  for (Size = 0; Size < BlockSize; ++Size)
  {
    Reset(Fixture);
    Fixture->Io.Size = Size;
    assert(EFI_ERROR(ReadFirst(Fixture)));
  }
}

STATIC VOID
TestObjectMapVersions(FIXTURE *Fixture)
{
  AC_APFS_CONTAINER    *Container;
  AC_APFS_VOLUME_INFO   Volume;
  CONST APFS_OMAP_KEY   RootKeys[] = {{1999, 1}, {2000, 11}};
  CONST APFS_OMAP_KEY   LeftKeys[] = {{1999, 1}, {2000, 5}};
  CONST APFS_OMAP_KEY   RightKeys[] = {{2000, 11}, {2001, 7}, {2002, 10}};
  CONST APFS_OMAP_KEY   LeafKeys[] = {{2000, 5}, {2001, 7}, {2002, 10}};
  CONST APFS_OMAP_VALUE LeftValues[] = {{0, BlockSize, FutureBlock}, {0, BlockSize, PrebootBlock}};
  CONST APFS_OMAP_VALUE RightValues[] = {
    {0, BlockSize, FutureBlock}, {0, BlockSize, RecoveryBlock}, {0, BlockSize, SystemBlock}};
  CONST UINT64          Children[] = {LeftBlock, RightBlock};
  CONST APFS_OMAP_VALUE LeafValues[] = {
    {0, BlockSize, PrebootBlock}, {0, BlockSize, RecoveryBlock}, {0, BlockSize, SystemBlock}};

  Reset(Fixture);
  WriteNode(Fixture, RootBlock, TRUE, 0, LeafKeys, LeafValues, NULL, 3);
  assert(ReadFirst(Fixture) == EFI_SUCCESS);

  Reset(Fixture);
  WriteNode(Fixture, RootBlock, TRUE, 1, RootKeys, NULL, Children, 2);
  WriteNode(Fixture, LeftBlock, FALSE, 0, LeftKeys, LeftValues, NULL, 2);
  WriteNode(Fixture, RightBlock, FALSE, 0, RightKeys, RightValues, NULL, 3);
  assert(AcApfsOpenContainer(&Fixture->Io, &Container) == EFI_SUCCESS);
  assert(AcApfsGetVolumeInfo(Container, 0, &Volume) == EFI_SUCCESS);
  assert(strcmp(Volume.Name, "Preboot") == 0);
  assert(AcApfsGetVolumeInfo(Container, 1, &Volume) == EFI_SUCCESS);
  AcApfsCloseContainer(Container);

  Reset(Fixture);
  WriteNode(Fixture, RootBlock, TRUE, 0, RightKeys, RightValues, NULL, 3);
  assert(ReadFirst(Fixture) == EFI_COMPROMISED_DATA);

  Reset(Fixture);
  Put64(Block(Fixture, NewNx), 0xB8, 1999);
  Seal(Fixture, NewNx);
  assert(ReadFirst(Fixture) == EFI_COMPROMISED_DATA);

  Reset(Fixture);
  Block(Fixture, PrebootBlock)[1000] ^= 1;
  assert(AcApfsOpenContainer(&Fixture->Io, &Container) == EFI_SUCCESS);
  assert(AcApfsGetVolumeInfo(Container, 0, &Volume) == EFI_COMPROMISED_DATA);
  assert(Volume.Name[0] == '\0');
  assert(AcApfsGetVolumeInfo(Container, 1, &Volume) == EFI_SUCCESS);
  assert(strcmp(Volume.Name, "Recovery") == 0);
  AcApfsCloseContainer(Container);
  assert(OutstandingPools == 0);
}

STATIC VOID
TestFailures(FIXTURE *Fixture)
{
  UINTN            Allocations;
  UINTN            Reads;
  UINTN            Index;
  UINTN            Error;
  CONST EFI_STATUS Errors[] = {EFI_DEVICE_ERROR, EFI_COMPROMISED_DATA, EFI_UNSUPPORTED};

  Reset(Fixture);
  assert(ReadFirst(Fixture) == EFI_SUCCESS);
  Allocations = AllocationCalls;
  Reads = Fixture->ReadCalls;
  for (Index = 1; Index <= Allocations; ++Index)
  {
    Reset(Fixture);
    FailAllocationAt = Index;
    assert(ReadFirst(Fixture) == EFI_OUT_OF_RESOURCES);
  }
  for (Error = 0; Error < sizeof(Errors) / sizeof(Errors[0]); ++Error)
  {
    for (Index = 1; Index <= Reads; ++Index)
    {
      Reset(Fixture);
      Fixture->FailReadAt = Index;
      Fixture->FailReadStatus = Errors[Error];
      assert(ReadFirst(Fixture) == Errors[Error]);
      assert(Fixture->ReadCalls == Index);
    }
  }
}

STATIC VOID
TestMutations(FIXTURE *Fixture)
{
  UINT32       State;
  UINT32       Iteration;
  UINT32       Number;
  UINT32       Offset;
  CONST UINT32 Targets[] = {0,  OldMap,    OldNx,     NewMap,    NewMap + 1, NewNx,       20,
                            21, OmapBlock, RootBlock, LeftBlock, RightBlock, PrebootBlock};

  State = 0x12345678;
  for (Iteration = 0; Iteration < 1000; ++Iteration)
  {
    Reset(Fixture);
    State = State * 1664525U + 1013904223U;
    Number = Targets[State % (sizeof(Targets) / sizeof(Targets[0]))];
    State = State * 1664525U + 1013904223U;
    Offset = 8 + State % (BlockSize - 8);
    Block(Fixture, Number)[Offset] ^= (UINT8)((State >> 24) | 1U);
    if ((Iteration & 1U) != 0)
      Seal(Fixture, Number);
    (VOID) ReadFirst(Fixture);
  }
}

int
main(void)
{
  FIXTURE Fixture;

  ZeroMem(&Fixture, sizeof(Fixture));
  Fixture.Storage = malloc(DiskSize + 1);
  assert(Fixture.Storage != NULL);
  Fixture.Disk = Fixture.Storage + 1;
  TestEnumeration(&Fixture);
  TestCheckpoints(&Fixture);
  TestBadMetadata(&Fixture);
  TestObjectMapVersions(&Fixture);
  TestFailures(&Fixture);
  TestMutations(&Fixture);
  free(Fixture.Storage);
  puts("APFS container tests passed.");
  return 0;
}
