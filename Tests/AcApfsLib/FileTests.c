/** @file
  Synthetic volume, directory, Unicode name, and extent read tests.
**/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Library/AcApfsLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

STATIC UINTN Pools;
STATIC UINTN Allocations;
STATIC UINTN FailAllocation;

VOID *EFIAPI
CopyMem(VOID *To, CONST VOID *From, UINTN Size)
{
  return memmove(To, From, Size);
}
VOID *EFIAPI
ZeroMem(VOID *To, UINTN Size)
{
  return memset(To, 0, Size);
}
INTN EFIAPI
CompareMem(CONST VOID *A, CONST VOID *B, UINTN Size)
{
  return memcmp(A, B, Size);
}
VOID *EFIAPI
AllocateZeroPool(UINTN Size)
{
  VOID *Result;
  if (++Allocations == FailAllocation)
    return NULL;
  Result = calloc(1, Size);
  if (Result != NULL)
    ++Pools;
  return Result;
}
VOID EFIAPI
FreePool(VOID *Buffer)
{
  assert(Buffer != NULL && Pools != 0);
  --Pools;
  free(Buffer);
}

enum
{
  Bs = 4096,
  Blocks = 256,
  DiskBytes = Bs * Blocks,
  RecordsPerLeaf = 6,
  MaxRecords = 64
};

typedef struct
{
  UINT8 Key[1100];
  UINT8 Value[256];
  UINTN KeySize;
  UINTN ValueSize;
} TEST_RECORD;

typedef struct
{
  UINT8      *Storage;
  UINT8      *Disk;
  AC_APFS_IO  Io;
  UINTN       Reads;
  UINTN       FailRead;
  UINT64      FailOffset;
  UINT64      Features;
  UINTN       Count;
  TEST_RECORD Records[MaxRecords];
  CHAR8       LongName[261];
} FIXTURE;

STATIC BOOLEAN SortHashed;

STATIC VOID
Put16(VOID *To, UINT16 V)
{
  CopyMem(To, &V, sizeof(V));
}
STATIC VOID
Put32(VOID *To, UINT32 V)
{
  CopyMem(To, &V, sizeof(V));
}
STATIC VOID
Put64(VOID *To, UINT64 V)
{
  CopyMem(To, &V, sizeof(V));
}
STATIC UINT16
Get16(CONST VOID *From)
{
  UINT16 V;
  CopyMem(&V, From, sizeof(V));
  return V;
}
STATIC UINT32
Get32(CONST VOID *From)
{
  UINT32 V;
  CopyMem(&V, From, sizeof(V));
  return V;
}
STATIC UINT64
Get64(CONST VOID *From)
{
  UINT64 V;
  CopyMem(&V, From, sizeof(V));
  return V;
}
STATIC UINT8 *
Block(FIXTURE *F, UINTN Number)
{
  assert(Number < Blocks);
  return F->Disk + Number * Bs;
}

STATIC VOID
Seal(FIXTURE *F, UINTN Number)
{
  UINT8 *Buffer = Block(F, Number);
  UINT64 A = 0, B = 0, Low, High;
  UINTN  I;
  for (I = 8; I < Bs; I += 4)
  {
    A = (A + Get32(Buffer + I)) % 0xFFFFFFFFULL;
    B = (B + A) % 0xFFFFFFFFULL;
  }
  Low = 0xFFFFFFFFULL - ((A + B) % 0xFFFFFFFFULL);
  High = 0xFFFFFFFFULL - ((A + Low) % 0xFFFFFFFFULL);
  Put64(Buffer, Low | (High << 32U));
}

STATIC EFI_STATUS EFIAPI
ReadDisk(VOID *Context, UINT64 Offset, UINTN Size, VOID *Buffer)
{
  FIXTURE *F = Context;
  ++F->Reads;
  if (F->Reads == F->FailRead || Offset == F->FailOffset)
    return EFI_DEVICE_ERROR;
  assert(Offset <= F->Io.Size && Size <= F->Io.Size - Offset);
  CopyMem(Buffer, F->Disk + Offset, Size);
  return EFI_SUCCESS;
}

STATIC int
CompareRecords(CONST VOID *First, CONST VOID *Second)
{
  CONST TEST_RECORD *A = First, *B = Second;
  UINT64             AH = Get64(A->Key), BH = Get64(B->Key);
  UINT64             AO = AH & APFS_ID_MASK, BO = BH & APFS_ID_MASK;
  UINTN              Prefix;
  UINT32             Type;
  if (AO != BO)
    return AO < BO ? -1 : 1;
  if ((AH >> 60U) != (BH >> 60U))
    return (AH >> 60U) < (BH >> 60U) ? -1 : 1;
  Type = (UINT32)(AH >> 60U);
  if (Type == APFS_RECORD_DIRECTORY)
  {
    Prefix = SortHashed ? 12 : 10;
    if (SortHashed && (Get32(A->Key + 8) >> 10U) != (Get32(B->Key + 8) >> 10U))
      return (Get32(A->Key + 8) >> 10U) < (Get32(B->Key + 8) >> 10U) ? -1 : 1;
    return strcmp((CONST CHAR8 *)A->Key + Prefix, (CONST CHAR8 *)B->Key + Prefix);
  }
  if (Type == APFS_RECORD_FILE_EXTENT)
    return Get64(A->Key + 8) == Get64(B->Key + 8)
             ? 0
             : (Get64(A->Key + 8) < Get64(B->Key + 8) ? -1 : 1);
  return 0;
}

STATIC TEST_RECORD *
AddRecord(FIXTURE *F, UINT64 Id, UINT32 Type)
{
  TEST_RECORD *R;
  assert(F->Count < MaxRecords);
  R = &F->Records[F->Count++];
  ZeroMem(R, sizeof(*R));
  R->KeySize = 8;
  Put64(R->Key, Id | ((UINT64)Type << 60U));
  return R;
}

STATIC VOID
AddInode(FIXTURE *F, UINT64 Id, UINT64 Parent, UINT16 Mode, UINT64 StreamId, UINT64 Size)
{
  TEST_RECORD *R = AddRecord(F, Id, APFS_RECORD_INODE);
  APFS_INODE   Inode;
  APFS_DSTREAM Stream;
  ZeroMem(&Inode, sizeof(Inode));
  Inode.ParentId = Parent;
  Inode.PrivateId = StreamId;
  Inode.Mode = Mode;
  CopyMem(R->Value, &Inode, sizeof(Inode));
  R->ValueSize = sizeof(Inode);
  if (Mode == APFS_MODE_REGULAR)
  {
    ZeroMem(&Stream, sizeof(Stream));
    Stream.Size = Size;
    Stream.AllocatedSize = Size == 0 ? 0 : (Size + Bs - 1) / Bs * Bs;
    Put16(R->Value + sizeof(Inode), 1);
    Put16(R->Value + sizeof(Inode) + 2, sizeof(Stream));
    R->Value[sizeof(Inode) + 4] = APFS_INODE_EXT_DSTREAM;
    Put16(R->Value + sizeof(Inode) + 6, sizeof(Stream));
    CopyMem(R->Value + sizeof(Inode) + 8, &Stream, sizeof(Stream));
    R->ValueSize += 8 + sizeof(Stream);
  }
}

STATIC VOID
AddDirectory(FIXTURE *F, UINT64 Parent, UINT64 Id, UINT16 Type, CONST CHAR8 *Name, UINT32 Hash)
{
  TEST_RECORD         *R = AddRecord(F, Parent, APFS_RECORD_DIRECTORY);
  APFS_DIRECTORY_VALUE Value;
  UINTN                Length = strlen(Name) + 1;
  BOOLEAN              Hashed = F->Features != 0;
  ZeroMem(&Value, sizeof(Value));
  Value.FileId = Id;
  Value.Flags = Type;
  CopyMem(R->Value, &Value, sizeof(Value));
  R->ValueSize = sizeof(Value);
  if (Hashed)
    Put32(R->Key + 8, (Hash << 10U) | (UINT32)Length);
  else
    Put16(R->Key + 8, (UINT16)Length);
  R->KeySize = (Hashed ? 12 : 10) + Length;
  CopyMem(R->Key + (Hashed ? 12 : 10), Name, Length);
}

STATIC VOID
AddExtent(FIXTURE *F, UINT64 Id, UINT64 Offset, UINT64 Physical, UINT64 Length)
{
  TEST_RECORD     *R = AddRecord(F, Id, APFS_RECORD_FILE_EXTENT);
  APFS_FILE_EXTENT Extent = {Length, Physical, 0};
  Put64(R->Key + 8, Offset);
  R->KeySize = 16;
  R->ValueSize = sizeof(Extent);
  CopyMem(R->Value, &Extent, sizeof(Extent));
}

STATIC VOID
WriteNode(FIXTURE *F, UINTN Number, UINT64 Oid, BOOLEAN Root, UINT16 Level, BOOLEAN Omap,
          CONST TEST_RECORD *Records, UINTN Count, UINT64 TotalKeys, UINT64 NodeCount)
{
  APFS_BTREE_NODE Node;
  APFS_BTREE_INFO Info;
  UINTN           TableSize = Count * (Omap ? 4 : 8) + 8;
  UINTN           KeyBase = sizeof(Node) + 8 + TableSize;
  UINTN           KeyOffset = 0, ValueOffset = 0;
  UINTN           ValueEnd = Bs - (Root ? sizeof(Info) : 0);
  UINTN           I;
  UINT8          *Entry;
  UINT8          *Buffer = Block(F, Number);
  ZeroMem(Buffer, Bs);
  ZeroMem(&Node, sizeof(Node));
  Node.Object.Oid = Oid;
  Node.Object.Xid = 20;
  Node.Object.Type =
    (Omap ? APFS_OBJECT_PHYSICAL : 0) | (Root ? APFS_OBJECT_BTREE : APFS_OBJECT_BTREE_NODE);
  Node.Object.Subtype = Omap ? APFS_OBJECT_OMAP : APFS_OBJECT_FSTREE;
  Node.Flags = (Root ? APFS_BTNODE_ROOT : 0) | (Level == 0 ? APFS_BTNODE_LEAF : 0) |
               (Omap ? APFS_BTNODE_FIXED_KV : 0);
  Node.Level = Level;
  Node.KeyCount = (UINT32)Count;
  Node.TableSpace.Offset = 8;
  Node.TableSpace.Length = (UINT16)TableSize;
  for (I = 0; I < Count; ++I)
  {
    ValueOffset += Records[I].ValueSize;
    assert(KeyBase + KeyOffset + Records[I].KeySize <= ValueEnd - ValueOffset);
    Entry = Buffer + sizeof(Node) + 8 + I * (Omap ? 4 : 8);
    Put16(Entry, (UINT16)KeyOffset);
    if (Omap)
      Put16(Entry + 2, (UINT16)ValueOffset);
    else
    {
      Put16(Entry + 2, (UINT16)Records[I].KeySize);
      Put16(Entry + 4, (UINT16)ValueOffset);
      Put16(Entry + 6, (UINT16)Records[I].ValueSize);
    }
    CopyMem(Buffer + KeyBase + KeyOffset, Records[I].Key, Records[I].KeySize);
    CopyMem(Buffer + ValueEnd - ValueOffset, Records[I].Value, Records[I].ValueSize);
    KeyOffset += Records[I].KeySize;
  }
  Node.FreeSpace.Offset = (UINT16)KeyOffset;
  Node.FreeSpace.Length = (UINT16)(ValueEnd - ValueOffset - KeyBase - KeyOffset);
  CopyMem(Buffer, &Node, sizeof(Node));
  if (Root)
  {
    ZeroMem(&Info, sizeof(Info));
    Info.Flags = Omap ? APFS_BTREE_PHYSICAL : APFS_BTREE_KV_NONALIGNED;
    Info.NodeSize = Bs;
    Info.KeySize = Omap ? 16 : 0;
    Info.ValueSize = Omap ? 16 : 0;
    Info.KeyCount = TotalKeys;
    Info.NodeCount = NodeCount;
    CopyMem(Buffer + ValueEnd, &Info, sizeof(Info));
  }
  Seal(F, Number);
}

STATIC VOID
WriteMap(FIXTURE *F, UINTN Number, UINTN Tree, BOOLEAN Container)
{
  APFS_OMAP Map;
  ZeroMem(&Map, sizeof(Map));
  Map.Object.Oid = Number;
  Map.Object.Xid = 20;
  Map.Object.Type = APFS_OBJECT_PHYSICAL | APFS_OBJECT_OMAP;
  Map.Flags = Container ? APFS_OMAP_MANUALLY_MANAGED : 0;
  Map.TreeType = APFS_OBJECT_PHYSICAL | APFS_OBJECT_BTREE;
  Map.TreeOid = Tree;
  CopyMem(Block(F, Number), &Map, sizeof(Map));
  Seal(F, Number);
}

STATIC VOID
MappingRecord(TEST_RECORD *R, UINT64 Oid, UINT64 Address)
{
  APFS_OMAP_KEY   Key = {Oid, 20};
  APFS_OMAP_VALUE Value = {0, Bs, Address};
  ZeroMem(R, sizeof(*R));
  R->KeySize = sizeof(Key);
  R->ValueSize = sizeof(Value);
  CopyMem(R->Key, &Key, sizeof(Key));
  CopyMem(R->Value, &Value, sizeof(Value));
}

STATIC VOID
WriteFileTree(FIXTURE *F)
{
  TEST_RECORD Branches[16];
  TEST_RECORD Mappings[16];
  UINTN       LeafCount = (F->Count + RecordsPerLeaf - 1) / RecordsPerLeaf;
  UINTN       I, Start, Count;
  assert(LeafCount < 16);
  SortHashed = F->Features != 0;
  qsort(F->Records, F->Count, sizeof(F->Records[0]), CompareRecords);
  MappingRecord(&Mappings[0], 5000, 40);
  for (I = 0; I < LeafCount; ++I)
  {
    Start = I * RecordsPerLeaf;
    Count = F->Count - Start < RecordsPerLeaf ? F->Count - Start : RecordsPerLeaf;
    WriteNode(F, 41 + I, 5001 + I, FALSE, 0, FALSE, &F->Records[Start], Count, 0, 0);
    ZeroMem(&Branches[I], sizeof(Branches[I]));
    Branches[I].KeySize = F->Records[Start].KeySize;
    CopyMem(Branches[I].Key, F->Records[Start].Key, Branches[I].KeySize);
    Branches[I].ValueSize = 8;
    Put64(Branches[I].Value, 5001 + I);
    MappingRecord(&Mappings[I + 1], 5001 + I, 41 + I);
  }
  WriteNode(F, 40, 5000, TRUE, 1, FALSE, Branches, LeafCount, F->Count, LeafCount + 1);
  WriteNode(F, 34, 34, TRUE, 0, TRUE, Mappings, LeafCount + 1, LeafCount + 1, 1);
}

STATIC VOID
Reset(FIXTURE *F, UINT64 Features)
{
  APFS_NX_SUPERBLOCK      Nx;
  APFS_CHECKPOINT_MAP     Map;
  APFS_CHECKPOINT_MAPPING Mapping;
  APFS_VOLUME_SUPERBLOCK  Volume;
  TEST_RECORD             Mappings[2];
  TEST_RECORD             Other[3];
  UINTN                   I;
  UINT64                  SavedFeatures;
  assert(Pools == 0);
  ZeroMem(F->Disk, DiskBytes);
  F->Reads = 0;
  F->FailRead = 0;
  F->FailOffset = MAX_UINT64;
  F->Features = Features;
  F->Count = 0;
  Allocations = 0;
  FailAllocation = 0;
  F->Io.Context = F;
  F->Io.Read = ReadDisk;
  F->Io.Size = DiskBytes;
  for (I = 0; I < 128; ++I)
  {
    F->LongName[I * 2] = (CHAR8)0xC3;
    F->LongName[I * 2 + 1] = (CHAR8)0xA9;
  }
  CopyMem(F->LongName + 256, ".bin", 5);
  AddInode(F, 2, 1, APFS_MODE_DIRECTORY, 2, 0);
  AddInode(F, 100, 2, APFS_MODE_DIRECTORY, 100, 0);
  AddInode(F, 200, 100, APFS_MODE_DIRECTORY, 200, 0);
  AddInode(F, 300, 200, APFS_MODE_REGULAR, 900, Bs + 173);
  AddInode(F, 301, 2, APFS_MODE_REGULAR, 901, 0);
  AddInode(F, 302, 2, APFS_MODE_REGULAR, 902, 3 * Bs - 17);
  AddInode(F, 303, 2, APFS_MODE_REGULAR, 900, Bs + 173);
  AddInode(F, 304, 2, APFS_MODE_SYMLINK, 304, 0);
  AddInode(F, 305, 2, APFS_MODE_REGULAR, 905, 0);
  Put32(F->Records[F->Count - 1].Value + OFFSET_OF(APFS_INODE, BsdFlags), APFS_BSD_COMPRESSED);
  AddInode(F, 306, 2, APFS_MODE_REGULAR, 906, 7);
  AddInode(F, 307, 2, APFS_MODE_REGULAR, 907, 5);
  AddInode(F, 308, 2, APFS_MODE_REGULAR, 908, 7);
  AddInode(F, 310, 2, APFS_MODE_REGULAR, 910, 0);
  AddInode(F, 311, 2, APFS_MODE_REGULAR, 911, 0);
  AddInode(F, 312, 2, APFS_MODE_REGULAR, 912, 0);
  AddInode(F, 400, 2, APFS_MODE_DIRECTORY, 400, 0);
  AddDirectory(F, 2, 100, 4, "System", (Features & 1) ? 2313547 : 1651532);
  AddDirectory(F, 100, 200, 4, "Library", (Features & 1) ? 1803578 : 1857390);
  AddDirectory(F, 200, 300, 8, "kernel.bin", 3043765);
  AddDirectory(F, 2, 301, 8, "empty", 776007);
  AddDirectory(F, 2, 302, 8, "sparse", 3333772);
  AddDirectory(F, 2, 303, 8, "clone", 2546893);
  AddDirectory(F, 2, 304, 10, "link", 2536849);
  AddDirectory(F, 2, 305, 8, "compressed", 3275561);
  AddDirectory(F, 2, 306, 8, "Caf\xC3\xA9", (Features & 1) ? 2648289 : 4172135);
  AddDirectory(F, 2, 307, 8,
               "Stra\xC3\x9F"
               "e",
               (Features & 1) ? 1338499 : 386874);
  AddDirectory(F, 2, 308, 8, "\xED\x95\x9C\xEA\xB8\x80", 3878811);
  AddDirectory(F, 2, 400, 4, "EmptyDir", (Features & 1) ? 2553091 : 2759826);
  AddDirectory(F, 2, 310, 8, "hash-9d105915", 2771503);
  AddDirectory(F, 2, 311, 8, "hash-57ebcccd", 2771503);
  AddDirectory(F, 2, 312, 8, F->LongName, 628482);
  AddExtent(F, 900, 0, 100, Bs);
  AddExtent(F, 900, Bs, 104, Bs);
  AddExtent(F, 902, 0, 110, Bs);
  AddExtent(F, 902, Bs, 0, Bs);
  AddExtent(F, 902, 2 * Bs, 114, Bs);
  AddExtent(F, 906, 0, 120, Bs);
  AddExtent(F, 907, 0, 121, Bs);
  AddExtent(F, 908, 0, 122, Bs);
  WriteFileTree(F);
  WriteMap(F, 32, 34, FALSE);
  WriteMap(F, 33, 35, FALSE);
  MappingRecord(&Mappings[0], 5000, 60);
  WriteNode(F, 35, 35, TRUE, 0, TRUE, Mappings, 1, 1, 1);
  SavedFeatures = F->Features;
  F->Features = 0;
  F->Count = 0;
  AddInode(F, 2, 1, APFS_MODE_DIRECTORY, 2, 0);
  AddDirectory(F, 2, 301, 8, "empty", 0);
  AddInode(F, 301, 2, APFS_MODE_REGULAR, 999, 0);
  CopyMem(Other, F->Records, sizeof(Other));
  SortHashed = FALSE;
  qsort(Other, 3, sizeof(Other[0]), CompareRecords);
  WriteNode(F, 60, 5000, TRUE, 0, FALSE, Other, 3, 3, 1);
  F->Features = SavedFeatures;
  F->Count = 0;
  ZeroMem(&Nx, sizeof(Nx));
  Nx.Object.Oid = 1;
  Nx.Object.Xid = 20;
  Nx.Object.Type = APFS_OBJECT_EPHEMERAL | APFS_OBJECT_NX_SUPERBLOCK;
  Nx.Magic = APFS_NX_MAGIC;
  Nx.BlockSize = Bs;
  Nx.BlockCount = Blocks;
  Nx.IncompatibleFeatures = APFS_NX_INCOMPAT_VERSION2;
  Nx.Uuid[0] = 0xA5;
  Nx.NextXid = 21;
  Nx.NextOid = 6000;
  Nx.DescriptorBase = 1;
  Nx.DescriptorBlocks = 4;
  Nx.DescriptorIndex = 0;
  Nx.DescriptorLength = 2;
  Nx.DescriptorNext = 2;
  Nx.DataBase = 8;
  Nx.DataBlocks = 8;
  Nx.DataLength = 2;
  Nx.DataNext = 2;
  Nx.SpacemanOid = 100;
  Nx.ReaperOid = 101;
  Nx.OmapOid = 16;
  Nx.MaxFileSystems = 100;
  Nx.FileSystemOid[0] = 1000;
  Nx.FileSystemOid[1] = 1001;
  CopyMem(Block(F, 0), &Nx, sizeof(Nx));
  Seal(F, 0);
  CopyMem(Block(F, 2), &Nx, sizeof(Nx));
  Seal(F, 2);
  ZeroMem(&Map, sizeof(Map));
  Map.Object.Oid = 1;
  Map.Object.Xid = 20;
  Map.Object.Type = APFS_OBJECT_PHYSICAL | APFS_OBJECT_CHECKPOINT_MAP;
  Map.Flags = APFS_CHECKPOINT_MAP_LAST;
  Map.Count = 2;
  CopyMem(Block(F, 1), &Map, sizeof(Map));
  for (I = 0; I < 2; ++I)
  {
    ZeroMem(&Mapping, sizeof(Mapping));
    Mapping.Oid = 100 + I;
    Mapping.Address = 8 + I;
    Mapping.Size = Bs;
    Mapping.Type = APFS_OBJECT_EPHEMERAL | (I == 0 ? APFS_OBJECT_SPACEMAN : APFS_OBJECT_REAPER);
    CopyMem(Block(F, 1) + sizeof(Map) + I * sizeof(Mapping), &Mapping, sizeof(Mapping));
    Put64(Block(F, 8 + I) + 8, Mapping.Oid);
    Put64(Block(F, 8 + I) + 16, 20);
    Put32(Block(F, 8 + I) + 24, Mapping.Type);
    Seal(F, 8 + I);
  }
  Seal(F, 1);
  WriteMap(F, 16, 17, TRUE);
  MappingRecord(&Mappings[0], 1000, 20);
  MappingRecord(&Mappings[1], 1001, 21);
  WriteNode(F, 17, 17, TRUE, 0, TRUE, Mappings, 2, 2, 1);
  for (I = 0; I < 2; ++I)
  {
    ZeroMem(&Volume, sizeof(Volume));
    Volume.Object.Oid = 1000 + I;
    Volume.Object.Xid = 20;
    Volume.Object.Type = APFS_OBJECT_VOLUME;
    Volume.Magic = APFS_VOLUME_MAGIC;
    Volume.FileSystemIndex = (UINT32)I;
    Volume.Flags = APFS_FS_UNENCRYPTED;
    Volume.OmapOid = 32 + I;
    Volume.RootTreeOid = 5000;
    Volume.RootTreeType = APFS_OBJECT_BTREE;
    Volume.IncompatibleFeatures = I == 0 ? Features : 0;
    Volume.Uuid[0] = (UINT8)(I + 1);
    Volume.Role = I == 0 ? APFS_VOLUME_ROLE_PREBOOT : APFS_VOLUME_ROLE_RECOVERY;
    CopyMem(Volume.Name, I == 0 ? "Preboot" : "Recovery", I == 0 ? 8 : 9);
    CopyMem(Block(F, 20 + I), &Volume, sizeof(Volume));
    Seal(F, 20 + I);
  }
  for (I = 0; I < Bs; ++I)
  {
    Block(F, 100)[I] = (UINT8)(I * 17 + 5);
    Block(F, 104)[I] = (UINT8)(I * 13 + 9);
    Block(F, 110)[I] = 0x6D;
    Block(F, 114)[I] = 0xA7;
  }
  CopyMem(Block(F, 120), "accent!", 7);
  CopyMem(Block(F, 121), "fold!", 5);
  CopyMem(Block(F, 122), "hangul!", 7);
}

#define CHECK_STATUS(Expression, Expected)                                                         \
  do                                                                                               \
  {                                                                                                \
    EFI_STATUS ActualStatus = (Expression);                                                        \
    if (ActualStatus != (Expected))                                                                \
      fprintf(stderr, "line %d: %s: got 0x%llx, expected 0x%llx\n", __LINE__, #Expression,         \
              (unsigned long long)ActualStatus, (unsigned long long)(Expected));                   \
    assert(ActualStatus == (Expected));                                                            \
  } while (0)

STATIC VOID
Open(FIXTURE *F, UINT32 Index, AC_APFS_CONTAINER **Container, AC_APFS_VOLUME **Volume)
{
  CHECK_STATUS(AcApfsOpenContainer(&F->Io, Container), EFI_SUCCESS);
  CHECK_STATUS(AcApfsOpenVolume(*Container, Index, Volume), EFI_SUCCESS);
}

STATIC VOID
Close(AC_APFS_CONTAINER *Container, AC_APFS_VOLUME *Volume)
{
  AcApfsCloseVolume(Volume);
  AcApfsCloseContainer(Container);
  assert(Pools == 0);
}

STATIC VOID
ExpectFile(AC_APFS_VOLUME *Volume, CONST CHAR8 *Path, UINT64 Id)
{
  AC_APFS_FILE     *File;
  AC_APFS_FILE_INFO Info;
  CHECK_STATUS(AcApfsOpenFile(Volume, Path, &File), EFI_SUCCESS);
  CHECK_STATUS(AcApfsGetFileInfo(File, &Info), EFI_SUCCESS);
  assert(Info.FileId == Id);
  AcApfsCloseFile(File);
}

STATIC VOID
TestPaths(FIXTURE *F)
{
  AC_APFS_CONTAINER *Container;
  AC_APFS_VOLUME    *Volume, *Other;
  AC_APFS_FILE      *File;
  CHAR8              Path[300];
  Reset(F, 9);
  Open(F, 0, &Container, &Volume);
  ExpectFile(Volume, "/", 2);
  ExpectFile(Volume, "///System//Library/./kernel.bin", 300);
  ExpectFile(Volume, "/System/Library/../Library/kernel.bin", 300);
  ExpectFile(Volume, "/../../System/Library/kernel.bin", 300);
  ExpectFile(Volume, "/system/library/KERNEL.BIN", 300);
  ExpectFile(Volume, "/CAF\xC3\x89", 306);
  ExpectFile(Volume, "/Cafe\xCC\x81", 306);
  ExpectFile(Volume, "/STRASSE", 307);
  ExpectFile(Volume, "/\xE1\x84\x92\xE1\x85\xA1\xE1\x86\xAB\xE1\x84\x80\xE1\x85\xB3\xE1\x86\xAF",
             308);
  ExpectFile(Volume, "/hash-9d105915", 310);
  ExpectFile(Volume, "/hash-57ebcccd", 311);
  Path[0] = '/';
  CopyMem(Path + 1, F->LongName, sizeof(F->LongName));
  ExpectFile(Volume, Path, 312);
  CHECK_STATUS(AcApfsOpenFile(Volume, "relative", &File), EFI_INVALID_PARAMETER);
  assert(File == NULL);
  CHECK_STATUS(AcApfsOpenFile(Volume, "", &File), EFI_INVALID_PARAMETER);
  CHECK_STATUS(AcApfsOpenFile(Volume, "/missing", &File), EFI_NOT_FOUND);
  CHECK_STATUS(AcApfsOpenFile(Volume, "/empty/", &File), EFI_NOT_FOUND);
  CHECK_STATUS(AcApfsOpenFile(Volume, "/empty/..", &File), EFI_NOT_FOUND);
  CHECK_STATUS(AcApfsOpenFile(Volume, "/link", &File), EFI_UNSUPPORTED);
  CHECK_STATUS(AcApfsOpenFile(Volume, "/link/child", &File), EFI_UNSUPPORTED);
  CHECK_STATUS(AcApfsOpenFile(Volume, "/compressed", &File), EFI_UNSUPPORTED);
  CHECK_STATUS(AcApfsOpenFile(Volume, "/\xC0\xAF", &File), EFI_INVALID_PARAMETER);
  CHECK_STATUS(AcApfsOpenFile(Volume, "/\xED\xA0\x80", &File), EFI_INVALID_PARAMETER);
  CHECK_STATUS(AcApfsOpenFile(Volume, "/\xF0\x9F\xA7\x90", &File), EFI_UNSUPPORTED);
  CHECK_STATUS(AcApfsOpenVolume(Container, 1, &Other), EFI_SUCCESS);
  ExpectFile(Other, "/empty", 301);
  CHECK_STATUS(AcApfsOpenFile(Other, "/System", &File), EFI_NOT_FOUND);
  ExpectFile(Volume, "/System", 100);
  AcApfsCloseVolume(Other);
  CHECK_STATUS(AcApfsOpenFile(NULL, "/", &File), EFI_INVALID_PARAMETER);
  CHECK_STATUS(AcApfsOpenFile(Volume, NULL, &File), EFI_INVALID_PARAMETER);
  CHECK_STATUS(AcApfsOpenVolume(Container, 9, &Other), EFI_NOT_FOUND);
  assert(Other == NULL);
  AcApfsCloseFile(NULL);
  AcApfsCloseVolume(NULL);
  Close(Container, Volume);
  Reset(F, 8);
  Open(F, 0, &Container, &Volume);
  ExpectFile(Volume, "/Cafe\xCC\x81", 306);
  CHECK_STATUS(AcApfsOpenFile(Volume, "/cafe\xCC\x81", &File), EFI_NOT_FOUND);
  CHECK_STATUS(AcApfsOpenFile(Volume, "/system", &File), EFI_NOT_FOUND);
  Close(Container, Volume);
  Reset(F, 0);
  Open(F, 0, &Container, &Volume);
  ExpectFile(Volume, "/Caf\xC3\xA9", 306);
  CHECK_STATUS(AcApfsOpenFile(Volume, "/Cafe\xCC\x81", &File), EFI_NOT_FOUND);
  Close(Container, Volume);
}

STATIC UINT8
ExpectedByte(UINT64 Offset)
{
  return Offset < Bs ? (UINT8)(Offset * 17 + 5) : (UINT8)((Offset - Bs) * 13 + 9);
}

STATIC VOID
TestReads(FIXTURE *F)
{
  AC_APFS_CONTAINER *Container;
  AC_APFS_VOLUME    *Volume;
  AC_APFS_FILE      *File, *Clone, *Empty;
  AC_APFS_FILE_INFO  Info;
  UINT8              Buffer[3 * Bs];
  UINTN              Size, I, Offset;
  Reset(F, 9);
  Open(F, 0, &Container, &Volume);
  CHECK_STATUS(AcApfsOpenFile(Volume, "/System/Library/kernel.bin", &File), EFI_SUCCESS);
  CHECK_STATUS(AcApfsGetFileInfo(File, &Info), EFI_SUCCESS);
  assert(Info.Size == Bs + 173 && Info.AllocatedSize == 2 * Bs);
  Size = sizeof(Buffer);
  CHECK_STATUS(AcApfsReadFile(File, 0, &Size, Buffer), EFI_SUCCESS);
  assert(Size == Bs + 173);
  for (I = 0; I < Size; ++I)
    assert(Buffer[I] == ExpectedByte(I));
  for (Offset = 0; Offset < Bs + 200; Offset += 137)
  {
    Size = 311;
    CHECK_STATUS(AcApfsReadFile(File, Offset, &Size, Buffer), EFI_SUCCESS);
    assert(Size == (Offset >= Bs + 173 ? 0 : (Bs + 173 - Offset < 311 ? Bs + 173 - Offset : 311)));
    for (I = 0; I < Size; ++I)
      assert(Buffer[I] == ExpectedByte(Offset + I));
  }
  Size = 12;
  CHECK_STATUS(AcApfsReadFile(File, MAX_UINT64, &Size, Buffer), EFI_SUCCESS);
  assert(Size == 0);
  Size = 0;
  CHECK_STATUS(AcApfsReadFile(File, 0, &Size, NULL), EFI_SUCCESS);
  Size = 1;
  CHECK_STATUS(AcApfsReadFile(File, 0, &Size, NULL), EFI_INVALID_PARAMETER);
  CHECK_STATUS(AcApfsReadFile(File, 0, NULL, Buffer), EFI_INVALID_PARAMETER);
  F->FailOffset = 104 * Bs;
  Size = sizeof(Buffer);
  CHECK_STATUS(AcApfsReadFile(File, 0, &Size, Buffer), EFI_DEVICE_ERROR);
  assert(Size == Bs);
  for (I = 0; I < Size; ++I)
    assert(Buffer[I] == ExpectedByte(I));
  F->FailOffset = MAX_UINT64;
  CHECK_STATUS(AcApfsOpenFile(Volume, "/clone", &Clone), EFI_SUCCESS);
  Size = sizeof(Buffer);
  CHECK_STATUS(AcApfsReadFile(Clone, 0, &Size, Buffer), EFI_SUCCESS);
  for (I = 0; I < Size; ++I)
    assert(Buffer[I] == ExpectedByte(I));
  AcApfsCloseFile(Clone);
  CHECK_STATUS(AcApfsOpenFile(Volume, "/sparse", &Clone), EFI_SUCCESS);
  Size = sizeof(Buffer);
  CHECK_STATUS(AcApfsReadFile(Clone, 0, &Size, Buffer), EFI_SUCCESS);
  assert(Size == 3 * Bs - 17);
  for (I = 0; I < Size; ++I)
    assert(Buffer[I] == (I < Bs ? 0x6D : (I < 2 * Bs ? 0 : 0xA7)));
  Size = 15;
  CHECK_STATUS(AcApfsReadFile(Clone, Bs - 3, &Size, Buffer), EFI_SUCCESS);
  assert(Buffer[0] == 0x6D && Buffer[2] == 0x6D && Buffer[3] == 0);
  AcApfsCloseFile(Clone);
  CHECK_STATUS(AcApfsOpenFile(Volume, "/empty", &Empty), EFI_SUCCESS);
  Size = MAX_UINTN;
  CHECK_STATUS(AcApfsReadFile(Empty, 0, &Size, Buffer), EFI_SUCCESS);
  assert(Size == 0);
  AcApfsCloseFile(Empty);
  AcApfsCloseFile(File);
  Close(Container, Volume);
}

STATIC VOID
TestDirectories(FIXTURE *F)
{
  AC_APFS_CONTAINER       *Container;
  AC_APFS_VOLUME          *Volume;
  AC_APFS_FILE            *Directory, *Other;
  UINT8                    Storage[1200];
  AC_APFS_DIRECTORY_ENTRY *Entry = (VOID *)Storage;
  CHAR8                    Names[16][1024];
  UINTN                    Count = 0, Size, I, J, Seen, Before;
  EFI_STATUS               Status;
  Reset(F, 9);
  Open(F, 0, &Container, &Volume);
  CHECK_STATUS(AcApfsOpenFile(Volume, "/", &Directory), EFI_SUCCESS);
  CHECK_STATUS(AcApfsOpenFile(Volume, "/", &Other), EFI_SUCCESS);
  Size = 0;
  CHECK_STATUS(AcApfsReadDirectory(Directory, &Size, NULL), EFI_BUFFER_TOO_SMALL);
  assert(Size > OFFSET_OF(AC_APFS_DIRECTORY_ENTRY, Name));
  Size = 1;
  CHECK_STATUS(AcApfsReadDirectory(Directory, &Size, Entry), EFI_BUFFER_TOO_SMALL);
  for (;;)
  {
    Size = sizeof(Storage);
    Status = AcApfsReadDirectory(Directory, &Size, Entry);
    if (Status == EFI_NOT_FOUND)
    {
      assert(Size == 0);
      break;
    }
    CHECK_STATUS(Status, EFI_SUCCESS);
    assert(Count < 16);
    assert(Size == OFFSET_OF(AC_APFS_DIRECTORY_ENTRY, Name) + strlen(Entry->Name) + 1);
    for (I = 0; I < Count; ++I)
      assert(strcmp(Names[I], Entry->Name) != 0);
    CopyMem(Names[Count++], Entry->Name, strlen(Entry->Name) + 1);
  }
  assert(Count == 13);
  Seen = 0;
  for (I = 0; I < Count; ++I)
    if (strcmp(Names[I], F->LongName) == 0)
      ++Seen;
  assert(Seen == 1);
  Size = sizeof(Storage);
  CHECK_STATUS(AcApfsReadDirectory(Other, &Size, Entry), EFI_SUCCESS);
  assert(strcmp(Entry->Name, Names[0]) == 0);
  CHECK_STATUS(AcApfsRewindDirectory(Directory), EFI_SUCCESS);
  for (I = 0; I < Count; ++I)
  {
    Size = 0;
    CHECK_STATUS(AcApfsReadDirectory(Directory, &Size, NULL), EFI_BUFFER_TOO_SMALL);
    Size = sizeof(Storage);
    CHECK_STATUS(AcApfsReadDirectory(Directory, &Size, Entry), EFI_SUCCESS);
    assert(strcmp(Entry->Name, Names[I]) == 0);
  }
  for (I = 0; I < Count; ++I)
  {
    CHECK_STATUS(AcApfsRewindDirectory(Directory), EFI_SUCCESS);
    for (J = 0; J < I; ++J)
    {
      Size = sizeof(Storage);
      CHECK_STATUS(AcApfsReadDirectory(Directory, &Size, Entry), EFI_SUCCESS);
    }
    Before = F->Reads;
    F->FailRead = Before + 1;
    Size = sizeof(Storage);
    Status = AcApfsReadDirectory(Directory, &Size, Entry);
    F->FailRead = 0;
    if (Status == EFI_DEVICE_ERROR)
    {
      Size = sizeof(Storage);
      CHECK_STATUS(AcApfsReadDirectory(Directory, &Size, Entry), EFI_SUCCESS);
    }
    else
      CHECK_STATUS(Status, EFI_SUCCESS);
    assert(strcmp(Entry->Name, Names[I]) == 0);
  }
  CHECK_STATUS(AcApfsRewindDirectory(Directory), EFI_SUCCESS);
  FailAllocation = Allocations + 1;
  Size = sizeof(Storage);
  CHECK_STATUS(AcApfsReadDirectory(Directory, &Size, Entry), EFI_OUT_OF_RESOURCES);
  FailAllocation = 0;
  Size = sizeof(Storage);
  CHECK_STATUS(AcApfsReadDirectory(Directory, &Size, Entry), EFI_SUCCESS);
  assert(strcmp(Entry->Name, Names[0]) == 0);
  Size = 1;
  CHECK_STATUS(AcApfsReadFile(Directory, 0, &Size, Storage), EFI_UNSUPPORTED);
  AcApfsCloseFile(Directory);
  AcApfsCloseFile(Other);
  CHECK_STATUS(AcApfsOpenFile(Volume, "/EmptyDir", &Directory), EFI_SUCCESS);
  Size = sizeof(Storage);
  CHECK_STATUS(AcApfsReadDirectory(Directory, &Size, Entry), EFI_NOT_FOUND);
  CHECK_STATUS(AcApfsRewindDirectory(Directory), EFI_SUCCESS);
  Size = sizeof(Storage);
  CHECK_STATUS(AcApfsReadDirectory(Directory, &Size, Entry), EFI_NOT_FOUND);
  AcApfsCloseFile(Directory);
  CHECK_STATUS(AcApfsOpenFile(Volume, "/empty", &Directory), EFI_SUCCESS);
  Size = sizeof(Storage);
  CHECK_STATUS(AcApfsReadDirectory(Directory, &Size, Entry), EFI_UNSUPPORTED);
  CHECK_STATUS(AcApfsRewindDirectory(Directory), EFI_UNSUPPORTED);
  AcApfsCloseFile(Directory);
  Close(Container, Volume);
}

STATIC EFI_STATUS
Exercise(FIXTURE *F)
{
  AC_APFS_CONTAINER *Container = NULL;
  AC_APFS_VOLUME    *Volume = NULL;
  AC_APFS_FILE      *File = NULL;
  UINT8              Buffer[Bs + 173];
  UINTN              Size = sizeof(Buffer);
  EFI_STATUS         Status;
  Status = AcApfsOpenContainer(&F->Io, &Container);
  if (!EFI_ERROR(Status))
    Status = AcApfsOpenVolume(Container, 0, &Volume);
  if (!EFI_ERROR(Status))
    Status = AcApfsOpenFile(Volume, "/System/Library/kernel.bin", &File);
  if (!EFI_ERROR(Status))
    Status = AcApfsReadFile(File, 0, &Size, Buffer);
  AcApfsCloseFile(File);
  AcApfsCloseVolume(Volume);
  AcApfsCloseContainer(Container);
  assert(Pools == 0);
  return Status;
}

STATIC VOID
TestFailures(FIXTURE *F)
{
  UINTN TotalAllocations, TotalReads, I;
  Reset(F, 9);
  CHECK_STATUS(Exercise(F), EFI_SUCCESS);
  TotalAllocations = Allocations;
  TotalReads = F->Reads;
  for (I = 1; I <= TotalAllocations; ++I)
  {
    Reset(F, 9);
    FailAllocation = I;
    CHECK_STATUS(Exercise(F), EFI_OUT_OF_RESOURCES);
  }
  for (I = 1; I <= TotalReads; ++I)
  {
    Reset(F, 9);
    F->FailRead = I;
    CHECK_STATUS(Exercise(F), EFI_DEVICE_ERROR);
    assert(F->Reads == I);
  }
}

STATIC VOID
TestLargeOffsets(FIXTURE *F)
{
  AC_APFS_CONTAINER *Container;
  AC_APFS_VOLUME    *Volume;
  AC_APFS_FILE      *File;
  AC_APFS_FILE_INFO  Info;
  UINT64             LogicalSize = 0x140000011ULL;
  UINT8              Buffer[64];
  UINTN              Size;
  UINTN              Index;
  Reset(F, 9);
  F->Count = 0;
  F->Features = 0;
  AddInode(F, 2, 1, APFS_MODE_DIRECTORY, 2, 0);
  AddDirectory(F, 2, 301, 8, "large", 0);
  AddInode(F, 301, 2, APFS_MODE_REGULAR, 999, LogicalSize);
  Put64(F->Records[F->Count - 1].Value + sizeof(APFS_INODE) + 16, 0);
  AddExtent(F, 999, 0, 0, (LogicalSize + Bs - 1) / Bs * Bs);
  SortHashed = FALSE;
  qsort(F->Records, F->Count, sizeof(F->Records[0]), CompareRecords);
  WriteNode(F, 60, 5000, TRUE, 0, FALSE, F->Records, F->Count, F->Count, 1);
  Open(F, 1, &Container, &Volume);
  CHECK_STATUS(AcApfsOpenFile(Volume, "/large", &File), EFI_SUCCESS);
  CHECK_STATUS(AcApfsGetFileInfo(File, &Info), EFI_SUCCESS);
  assert(Info.Size == LogicalSize && Info.AllocatedSize == 0);
  Size = sizeof(Buffer);
  CHECK_STATUS(AcApfsReadFile(File, 0x100000003ULL, &Size, Buffer), EFI_SUCCESS);
  assert(Size == sizeof(Buffer));
  for (Index = 0; Index < Size; ++Index)
    assert(Buffer[Index] == 0);
  Size = sizeof(Buffer);
  CHECK_STATUS(AcApfsReadFile(File, LogicalSize - 7, &Size, Buffer), EFI_SUCCESS);
  assert(Size == 7);
  Size = sizeof(Buffer);
  CHECK_STATUS(AcApfsReadFile(File, LogicalSize, &Size, Buffer), EFI_SUCCESS);
  assert(Size == 0);
  AcApfsCloseFile(File);
  Close(Container, Volume);
}

STATIC UINT8 *
FindValue(FIXTURE *F, UINT64 Id, UINT32 Type, UINT64 Logical, UINTN *Number)
{
  UINTN  B, I, Table, KeyBase, ValueEnd;
  UINT8 *Entry, *Key;
  for (B = 41; B < 55; ++B)
  {
    UINT8 *Buffer = Block(F, B);
    if (Get32(Buffer + 36) == 0)
      continue;
    Table = sizeof(APFS_BTREE_NODE) + Get16(Buffer + 40);
    KeyBase = Table + Get16(Buffer + 42);
    ValueEnd = Bs;
    for (I = 0; I < Get32(Buffer + 36); ++I)
    {
      Entry = Buffer + Table + I * 8;
      Key = Buffer + KeyBase + Get16(Entry);
      if (Get64(Key) == (Id | ((UINT64)Type << 60U)) &&
          (Type != APFS_RECORD_FILE_EXTENT || Get64(Key + 8) == Logical) &&
          (Type != APFS_RECORD_DIRECTORY || Get64(Buffer + ValueEnd - Get16(Entry + 4)) == Logical))
      {
        *Number = B;
        return Buffer + ValueEnd - Get16(Entry + 4);
      }
    }
  }
  assert(FALSE);
  return NULL;
}

STATIC VOID
TestMalformed(FIXTURE *F)
{
  UINT8             *Value;
  UINTN              Number;
  AC_APFS_CONTAINER *Container;
  AC_APFS_VOLUME    *Volume;
  Reset(F, 9);
  Value = FindValue(F, 2, APFS_RECORD_DIRECTORY, 100, &Number);
  Put16(Value + OFFSET_OF(APFS_DIRECTORY_VALUE, Flags), 0);
  Seal(F, Number);
  CHECK_STATUS(Exercise(F), EFI_SUCCESS);
  Reset(F, 9);
  Value = FindValue(F, 2, APFS_RECORD_DIRECTORY, 100, &Number);
  Put16(Value + OFFSET_OF(APFS_DIRECTORY_VALUE, Flags), 8);
  Seal(F, Number);
  CHECK_STATUS(Exercise(F), EFI_COMPROMISED_DATA);
  Reset(F, 9);
  Value = FindValue(F, 300, APFS_RECORD_INODE, 0, &Number);
  Put16(Value + sizeof(APFS_INODE) + 2, 39);
  Seal(F, Number);
  CHECK_STATUS(Exercise(F), EFI_COMPROMISED_DATA);
  Reset(F, 9);
  Value = FindValue(F, 900, APFS_RECORD_FILE_EXTENT, Bs, &Number);
  Put64(Value + 8, MAX_UINT64);
  Seal(F, Number);
  CHECK_STATUS(Exercise(F), EFI_COMPROMISED_DATA);
  Reset(F, 9);
  Value = FindValue(F, 900, APFS_RECORD_FILE_EXTENT, 0, &Number);
  Put64(Value, 2 * Bs);
  Seal(F, Number);
  CHECK_STATUS(Exercise(F), EFI_COMPROMISED_DATA);
  Reset(F, 9);
  Value = FindValue(F, 900, APFS_RECORD_FILE_EXTENT, 0, &Number);
  Put64(Value, 0);
  Seal(F, Number);
  CHECK_STATUS(Exercise(F), EFI_COMPROMISED_DATA);
  Reset(F, 9);
  Value = FindValue(F, 900, APFS_RECORD_FILE_EXTENT, 0, &Number);
  Put64(Value + 16, 1);
  Seal(F, Number);
  CHECK_STATUS(Exercise(F), EFI_UNSUPPORTED);
  Reset(F, 9);
  Put16(Block(F, 40) + sizeof(APFS_BTREE_NODE) + 8 + 2, 0xFFFF);
  Seal(F, 40);
  CHECK_STATUS(Exercise(F), EFI_COMPROMISED_DATA);
  Reset(F, 9);
  Put64(Block(F, 20) + OFFSET_OF(APFS_VOLUME_SUPERBLOCK, IncompatibleFeatures), 0x20);
  Seal(F, 20);
  CHECK_STATUS(AcApfsOpenContainer(&F->Io, &Container), EFI_SUCCESS);
  CHECK_STATUS(AcApfsOpenVolume(Container, 0, &Volume), EFI_UNSUPPORTED);
  assert(Volume == NULL);
  AcApfsCloseContainer(Container);
  assert(Pools == 0);
  Reset(F, 9);
  Put64(Block(F, 20) + OFFSET_OF(APFS_VOLUME_SUPERBLOCK, Flags), 0);
  Seal(F, 20);
  CHECK_STATUS(Exercise(F), EFI_UNSUPPORTED);
  Reset(F, 9);
  Put64(Block(F, 20) + OFFSET_OF(APFS_VOLUME_SUPERBLOCK, RootToXid), 10);
  Seal(F, 20);
  CHECK_STATUS(Exercise(F), EFI_UNSUPPORTED);
}

STATIC VOID
TestMutations(FIXTURE *F)
{
  UINT32      State = 0xDEADBEEFU;
  UINTN       I, Number, Offset;
  CONST UINTN Targets[] = {20, 32, 34, 40, 41, 42, 43, 44, 45, 46, 47};
  for (I = 0; I < 500; ++I)
  {
    Reset(F, 9);
    State = State * 1664525U + 1013904223U;
    Number = Targets[State % (sizeof(Targets) / sizeof(Targets[0]))];
    State = State * 1664525U + 1013904223U;
    Offset = 8 + State % (Bs - 8);
    Block(F, Number)[Offset] ^= (UINT8)((State >> 24U) | 1U);
    if ((I & 1U) != 0)
      Seal(F, Number);
    (VOID) Exercise(F);
  }
}

int
main(void)
{
  FIXTURE *Fixture = calloc(1, sizeof(*Fixture));
  assert(Fixture != NULL);
  Fixture->Storage = malloc(DiskBytes + 1);
  assert(Fixture->Storage != NULL);
  Fixture->Disk = Fixture->Storage + 1;
  TestPaths(Fixture);
  TestReads(Fixture);
  TestDirectories(Fixture);
  TestMalformed(Fixture);
  TestLargeOffsets(Fixture);
  TestFailures(Fixture);
  TestMutations(Fixture);
  free(Fixture->Storage);
  free(Fixture);
  puts("APFS file tests passed (paths, directories, extents, failure injection, 500 mutations).");
  return 0;
}
