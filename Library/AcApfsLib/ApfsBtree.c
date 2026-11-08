/** @file
  Object Map and file-system B-tree lookup and ordered traversal.
**/

#include "ApfsInternal.h"

typedef struct
{
  VOID           *Buffer;
  APFS_BTREE_NODE Node;
  UINTN           KeyBase;
  UINTN           ValueEnd;
  UINT32          Index;
} APFS_FRAME;

struct APFS_CURSOR
{
  APFS_TREE   Tree;
  APFS_FRAME *Frames;
  UINTN       Height;
  BOOLEAN     End;
};

STATIC INTN
CompareNumbers(UINT64 A, UINT64 B)
{
  return A == B ? 0 : (A < B ? -1 : 1);
}

STATIC EFI_STATUS
CheckKey(CONST APFS_TREE *Tree, CONST UINT8 *Key, UINTN Size, BOOLEAN Disk)
{
  UINT32 Type;
  UINTN  Prefix;
  UINTN  Length;
  UINTN  Index;

  if (Tree->Subtype == APFS_OBJECT_OMAP)
    return Size == sizeof(APFS_OMAP_KEY) ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
  if (Size < sizeof(UINT64))
    return EFI_COMPROMISED_DATA;
  Type = (UINT32)(ApfsRead64(Key) >> 60U);
  switch (Type)
  {
  case 0:
    if (Disk)
      return EFI_COMPROMISED_DATA;
    // A prefix search starts before all records of an object.
    return Size == 8 ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
  case 1:
  case 2:
  case APFS_RECORD_INODE:
  case 6:
  case 7:
  case 10:
  case 12:
    return Size == 8 ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
  case 5:
  case APFS_RECORD_FILE_EXTENT:
  case 13:
    return Size == 16 ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
  case APFS_RECORD_XATTR:
  case APFS_RECORD_DIRECTORY:
  case 11:
    Prefix = (Type == APFS_RECORD_DIRECTORY && Tree->HashedNames) ? 12 : 10;
    if (Size < Prefix)
      return EFI_COMPROMISED_DATA;
    Length = Prefix == 12 ? ApfsRead32(Key + 8) & APFS_NAME_LENGTH_MASK : ApfsRead16(Key + 8);
    if (Length == 0 || Length != Size - Prefix || Key[Size - 1] != 0 || (Disk && Length == 1))
      return EFI_COMPROMISED_DATA;
    for (Index = Prefix; Index < Size - 1; ++Index)
      if (Key[Index] == 0 || Key[Index] == '/')
        return EFI_COMPROMISED_DATA;
    return EFI_SUCCESS;
  default:
    return EFI_UNSUPPORTED;
  }
}

STATIC INTN
CompareKeys(CONST APFS_TREE *Tree, CONST UINT8 *A, UINTN ASize, CONST UINT8 *B, UINTN BSize)
{
  INTN   Result;
  UINT64 AHeader;
  UINT64 BHeader;
  UINT32 Type;
  UINTN  Prefix;
  UINTN  Length;

  AHeader = ApfsRead64(A);
  BHeader = ApfsRead64(B);
  if (Tree->Subtype == APFS_OBJECT_OMAP)
  {
    Result = CompareNumbers(AHeader, BHeader);
    return Result != 0 ? Result : CompareNumbers(ApfsRead64(A + 8), ApfsRead64(B + 8));
  }
  Result = CompareNumbers(AHeader & APFS_ID_MASK, BHeader & APFS_ID_MASK);
  if (Result != 0)
    return Result;
  Type = (UINT32)(AHeader >> 60U);
  Result = CompareNumbers(Type, BHeader >> 60U);
  if (Result != 0 || ASize == 8)
    return Result;
  if (Type == 5 || Type == APFS_RECORD_FILE_EXTENT || Type == 13)
    return CompareNumbers(ApfsRead64(A + 8), ApfsRead64(B + 8));
  Prefix = 10;
  if (Type == APFS_RECORD_DIRECTORY && Tree->HashedNames)
  {
    Result = CompareNumbers(ApfsRead32(A + 8) >> 10U, ApfsRead32(B + 8) >> 10U);
    if (Result != 0)
      return Result;
    Prefix = 12;
  }
  Length = ASize - Prefix < BSize - Prefix ? ASize - Prefix : BSize - Prefix;
  Result = CompareMem(A + Prefix, B + Prefix, Length);
  return Result != 0 ? Result : CompareNumbers(ASize, BSize);
}

STATIC VOID
GetRecord(CONST APFS_FRAME *Frame, UINT32 Index, APFS_RECORD *Record)
{
  CONST UINT8 *Entry;
  UINTN        KeyOffset;
  UINTN        ValueOffset;

  Entry = (CONST UINT8 *)Frame->Buffer + sizeof(Frame->Node) + Frame->Node.TableSpace.Offset;
  if ((Frame->Node.Flags & APFS_BTNODE_FIXED_KV) != 0)
  {
    Entry += Index * sizeof(APFS_KV_OFFSET);
    KeyOffset = ApfsRead16(Entry);
    ValueOffset = ApfsRead16(Entry + 2);
    Record->KeySize = sizeof(APFS_OMAP_KEY);
    Record->ValueSize = Frame->Node.Level == 0 ? sizeof(APFS_OMAP_VALUE) : sizeof(UINT64);
  }
  else
  {
    Entry += Index * sizeof(APFS_KV_LOCATION);
    KeyOffset = ApfsRead16(Entry);
    Record->KeySize = ApfsRead16(Entry + 2);
    ValueOffset = ApfsRead16(Entry + 4);
    Record->ValueSize = ApfsRead16(Entry + 6);
  }
  Record->Key = (CONST UINT8 *)Frame->Buffer + Frame->KeyBase + KeyOffset;
  Record->Value = (CONST UINT8 *)Frame->Buffer + Frame->ValueEnd - ValueOffset;
}

STATIC EFI_STATUS
LoadFrame(CONST APFS_TREE *Tree, UINT64 Oid, BOOLEAN Root, UINT16 Level, OUT APFS_FRAME *Frame)
{
  APFS_OMAP_KEY   MappingKey;
  APFS_OMAP_VALUE Mapping;
  APFS_BTREE_INFO Info;
  APFS_RECORD     Record;
  APFS_RECORD     Previous;
  EFI_STATUS      Status;
  UINT64          Address;
  UINT64          Xid;
  UINT32          Index;
  UINTN           EntrySize;
  UINTN           KeyOffset;
  UINTN           KeySize;
  UINTN           ValueOffset;
  UINTN           ValueSize;
  UINTN           FreeStart;
  CONST UINT8    *Entry;
  UINT32          Storage;
  BOOLEAN         Fixed;

  ZeroMem(Frame, sizeof(*Frame));
  Address = Oid;
  Xid = 0;
  if (!Tree->Physical)
  {
    Status =
      ApfsLookupObject(Tree->Container, Tree->OmapRoot, Tree->Xid, Oid, &MappingKey, &Mapping);
    if (EFI_ERROR(Status))
      return Status == EFI_NOT_FOUND ? EFI_COMPROMISED_DATA : Status;
    if (Mapping.Size != Tree->Container->Superblock.BlockSize)
      return EFI_UNSUPPORTED;
    Address = Mapping.Address;
    Xid = MappingKey.Xid;
  }
  Status = ApfsReadObject(Tree->Container, Address, Tree->Container->Superblock.BlockSize,
                          &Frame->Buffer, NULL);
  if (EFI_ERROR(Status))
    return Status;
  CopyMem(&Frame->Node, Frame->Buffer, sizeof(Frame->Node));
  Storage = Tree->Physical ? APFS_OBJECT_PHYSICAL : 0;
  Status = EFI_COMPROMISED_DATA;
  if (Frame->Node.Object.Oid != Oid || Frame->Node.Object.Xid > Tree->Xid ||
      (!Tree->Physical && Frame->Node.Object.Xid != Xid) ||
      Frame->Node.Object.Type != (Storage | (Root ? APFS_OBJECT_BTREE : APFS_OBJECT_BTREE_NODE)) ||
      Frame->Node.Object.Subtype != Tree->Subtype ||
      (((Frame->Node.Flags & APFS_BTNODE_ROOT) != 0) != Root) ||
      (((Frame->Node.Flags & APFS_BTNODE_LEAF) != 0) != (Frame->Node.Level == 0)) ||
      (Frame->Node.Flags & ~(APFS_BTNODE_ROOT | APFS_BTNODE_LEAF | APFS_BTNODE_FIXED_KV)) != 0 ||
      (!Root && Frame->Node.Level != Level) || Frame->Node.Level >= 64)
    goto Failure;
  Fixed = (Frame->Node.Flags & APFS_BTNODE_FIXED_KV) != 0;
  if (Fixed != (Tree->Subtype == APFS_OBJECT_OMAP))
    goto Failure;
  Frame->ValueEnd = Tree->Container->Superblock.BlockSize;
  if (Root)
  {
    Frame->ValueEnd -= sizeof(Info);
    CopyMem(&Info, (UINT8 *)Frame->Buffer + Frame->ValueEnd, sizeof(Info));
    if (Info.NodeSize != Tree->Container->Superblock.BlockSize || Info.NodeCount == 0 ||
        Info.NodeCount > Tree->Container->Superblock.BlockCount ||
        Frame->Node.Level >= Info.NodeCount || Info.KeyCount < Frame->Node.KeyCount ||
        Info.KeySize != (Fixed ? sizeof(APFS_OMAP_KEY) : 0) ||
        Info.ValueSize != (Fixed ? sizeof(APFS_OMAP_VALUE) : 0) ||
        (Frame->Node.Level == 0 && (Info.NodeCount != 1 || Info.KeyCount != Frame->Node.KeyCount)))
      goto Failure;
    if ((((Info.Flags & APFS_BTREE_PHYSICAL) != 0) != Tree->Physical) ||
        (Info.Flags &
         ~(APFS_BTREE_PHYSICAL | APFS_BTREE_SEQUENTIAL_INSERT | APFS_BTREE_KV_NONALIGNED)) != 0)
    {
      Status = EFI_UNSUPPORTED;
      goto Failure;
    }
  }
  EntrySize = Fixed ? sizeof(APFS_KV_OFFSET) : sizeof(APFS_KV_LOCATION);
  Frame->KeyBase =
    sizeof(Frame->Node) + Frame->Node.TableSpace.Offset + Frame->Node.TableSpace.Length;
  if (Frame->KeyBase > Frame->ValueEnd ||
      Frame->Node.KeyCount > Frame->Node.TableSpace.Length / EntrySize ||
      (Frame->Node.KeyCount == 0 && (!Root || Frame->Node.Level != 0)))
    goto Failure;
  FreeStart = Frame->KeyBase + Frame->Node.FreeSpace.Offset;
  if (FreeStart > Frame->ValueEnd || Frame->Node.FreeSpace.Length > Frame->ValueEnd - FreeStart)
    goto Failure;
  for (Index = 0; Index < Frame->Node.KeyCount; ++Index)
  {
    Entry = (UINT8 *)Frame->Buffer + sizeof(Frame->Node) + Frame->Node.TableSpace.Offset +
            Index * EntrySize;
    KeyOffset = ApfsRead16(Entry);
    ValueOffset = ApfsRead16(Entry + (Fixed ? 2 : 4));
    KeySize = Fixed ? sizeof(APFS_OMAP_KEY) : ApfsRead16(Entry + 2);
    ValueSize =
      Fixed ? (Frame->Node.Level == 0 ? sizeof(APFS_OMAP_VALUE) : 8) : ApfsRead16(Entry + 6);
    if (KeyOffset == MAX_UINT16 || ValueOffset == MAX_UINT16 ||
        KeyOffset > FreeStart - Frame->KeyBase ||
        KeySize > FreeStart - Frame->KeyBase - KeyOffset || ValueOffset < ValueSize ||
        ValueOffset > Frame->ValueEnd - Frame->KeyBase ||
        Frame->ValueEnd - ValueOffset < FreeStart + Frame->Node.FreeSpace.Length ||
        (Frame->Node.Level != 0 && ValueSize != sizeof(UINT64)))
      goto Failure;
    GetRecord(Frame, Index, &Record);
    Status = CheckKey(Tree, Record.Key, Record.KeySize, TRUE);
    if (EFI_ERROR(Status))
      goto Failure;
    Status = EFI_COMPROMISED_DATA;
    if (Index != 0 &&
        CompareKeys(Tree, Previous.Key, Previous.KeySize, Record.Key, Record.KeySize) >= 0)
      goto Failure;
    Previous = Record;
  }
  return EFI_SUCCESS;

Failure:
  FreePool(Frame->Buffer);
  Frame->Buffer = NULL;
  return Status;
}

STATIC EFI_STATUS
CheckPath(CONST APFS_CURSOR *Cursor, UINTN Depth)
{
  APFS_RECORD       First;
  APFS_RECORD       Last;
  APFS_RECORD       Boundary;
  UINTN             Index;
  CONST APFS_FRAME *Frame;

  Frame = &Cursor->Frames[Depth];
  GetRecord(Frame, 0, &First);
  GetRecord(Frame, Frame->Node.KeyCount - 1, &Last);
  for (Index = 0; Index < Depth; ++Index)
  {
    Frame = &Cursor->Frames[Index];
    GetRecord(Frame, Frame->Index, &Boundary);
    if (CompareKeys(&Cursor->Tree, First.Key, First.KeySize, Boundary.Key, Boundary.KeySize) < 0)
      return EFI_COMPROMISED_DATA;
    if (Frame->Index + 1 < Frame->Node.KeyCount)
    {
      GetRecord(Frame, Frame->Index + 1, &Boundary);
      if (CompareKeys(&Cursor->Tree, Last.Key, Last.KeySize, Boundary.Key, Boundary.KeySize) >= 0)
        return EFI_COMPROMISED_DATA;
    }
  }
  return EFI_SUCCESS;
}

EFI_STATUS
ApfsBtreeCheckRoot(CONST APFS_TREE *Tree)
{
  APFS_FRAME Frame;
  EFI_STATUS Status;

  Status = LoadFrame(Tree, Tree->Root, TRUE, 0, &Frame);
  if (!EFI_ERROR(Status))
    FreePool(Frame.Buffer);
  return Status;
}

VOID
ApfsBtreeClose(APFS_CURSOR *Cursor OPTIONAL)
{
  UINTN Depth;
  if (Cursor == NULL)
    return;
  if (Cursor->Frames != NULL)
  {
    for (Depth = 0; Depth < Cursor->Height; ++Depth)
      if (Cursor->Frames[Depth].Buffer != NULL)
        FreePool(Cursor->Frames[Depth].Buffer);
    FreePool(Cursor->Frames);
  }
  FreePool(Cursor);
}

EFI_STATUS
ApfsBtreeRecord(CONST APFS_CURSOR *Cursor, OUT APFS_RECORD *Record)
{
  CONST APFS_FRAME *Leaf;
  if (Cursor->End)
    return EFI_NOT_FOUND;
  Leaf = &Cursor->Frames[Cursor->Height - 1];
  GetRecord(Leaf, Leaf->Index, Record);
  return EFI_SUCCESS;
}

EFI_STATUS
ApfsBtreeNext(APFS_CURSOR *Cursor)
{
  APFS_CURSOR Next;
  APFS_RECORD Record;
  APFS_FRAME *Frame;
  UINTN       Branch;
  UINTN       Depth;
  EFI_STATUS  Status;

  if (Cursor->End)
    return EFI_NOT_FOUND;
  Frame = &Cursor->Frames[Cursor->Height - 1];
  if (Frame->Index + 1 < Frame->Node.KeyCount)
  {
    ++Frame->Index;
    return EFI_SUCCESS;
  }
  Branch = Cursor->Height - 1;
  while (Branch != 0)
  {
    --Branch;
    if (Cursor->Frames[Branch].Index + 1 < Cursor->Frames[Branch].Node.KeyCount)
      break;
  }
  if (Cursor->Height == 1 ||
      Cursor->Frames[Branch].Index + 1 >= Cursor->Frames[Branch].Node.KeyCount)
  {
    Cursor->End = TRUE;
    return EFI_NOT_FOUND;
  }
  Next = *Cursor;
  Next.Frames = AllocateZeroPool(Cursor->Height * sizeof(*Next.Frames));
  if (Next.Frames == NULL)
    return EFI_OUT_OF_RESOURCES;
  CopyMem(Next.Frames, Cursor->Frames, (Branch + 1) * sizeof(*Next.Frames));
  ++Next.Frames[Branch].Index;
  for (Depth = Branch + 1; Depth < Cursor->Height; ++Depth)
  {
    Frame = &Next.Frames[Depth - 1];
    GetRecord(Frame, Frame->Index, &Record);
    Status = LoadFrame(&Next.Tree, ApfsRead64(Record.Value), FALSE, (UINT16)(Frame->Node.Level - 1),
                       &Next.Frames[Depth]);
    if (!EFI_ERROR(Status))
      Status = CheckPath(&Next, Depth);
    if (EFI_ERROR(Status))
    {
      for (Depth = Branch + 1; Depth < Cursor->Height; ++Depth)
        if (Next.Frames[Depth].Buffer != NULL)
          FreePool(Next.Frames[Depth].Buffer);
      FreePool(Next.Frames);
      return Status;
    }
  }
  for (Depth = Branch + 1; Depth < Cursor->Height; ++Depth)
    FreePool(Cursor->Frames[Depth].Buffer);
  FreePool(Cursor->Frames);
  Cursor->Frames = Next.Frames;
  return EFI_SUCCESS;
}

EFI_STATUS
ApfsBtreeSeek(CONST APFS_TREE *Tree, CONST VOID *Search, UINTN SearchSize, BOOLEAN Floor,
              OUT APFS_CURSOR **Cursor)
{
  APFS_CURSOR *Result;
  APFS_FRAME   Root;
  APFS_FRAME  *Frame;
  APFS_RECORD  Record;
  EFI_STATUS   Status;
  UINTN        Depth;
  UINT32       Index;
  UINT32       Selected;
  INTN         Comparison;

  *Cursor = NULL;
  Status = CheckKey(Tree, Search, SearchSize, FALSE);
  if (EFI_ERROR(Status))
    return Status;
  Status = LoadFrame(Tree, Tree->Root, TRUE, 0, &Root);
  if (EFI_ERROR(Status))
    return Status;
  Result = AllocateZeroPool(sizeof(*Result));
  if (Result == NULL)
  {
    FreePool(Root.Buffer);
    return EFI_OUT_OF_RESOURCES;
  }
  Result->Tree = *Tree;
  Result->Height = (UINTN)Root.Node.Level + 1;
  Result->Frames = AllocateZeroPool(Result->Height * sizeof(*Result->Frames));
  if (Result->Frames == NULL)
  {
    FreePool(Root.Buffer);
    ApfsBtreeClose(Result);
    return EFI_OUT_OF_RESOURCES;
  }
  Result->Frames[0] = Root;
  Status = EFI_NOT_FOUND;
  if (Root.Node.KeyCount == 0)
    goto Failure;
  for (Depth = 0; Depth < Result->Height; ++Depth)
  {
    Frame = &Result->Frames[Depth];
    if (Depth != 0)
    {
      Status = CheckPath(Result, Depth);
      if (EFI_ERROR(Status))
        goto Failure;
    }
    Selected = MAX_UINT32;
    for (Index = 0; Index < Frame->Node.KeyCount; ++Index)
    {
      GetRecord(Frame, Index, &Record);
      Comparison = CompareKeys(Tree, Record.Key, Record.KeySize, Search, SearchSize);
      if (Frame->Node.Level == 0 && !Floor)
      {
        if (Comparison >= 0)
        {
          Selected = Index;
          break;
        }
      }
      else if (Comparison <= 0)
        Selected = Index;
      else
        break;
    }
    if (Selected == MAX_UINT32)
    {
      if (Floor)
      {
        Status = EFI_NOT_FOUND;
        goto Failure;
      }
      if (Frame->Node.Level != 0)
        Selected = 0;
      else
      {
        Frame->Index = Frame->Node.KeyCount - 1;
        Status = ApfsBtreeNext(Result);
        if (EFI_ERROR(Status))
          goto Failure;
        *Cursor = Result;
        return EFI_SUCCESS;
      }
    }
    Frame->Index = Selected;
    if (Frame->Node.Level != 0)
    {
      GetRecord(Frame, Selected, &Record);
      Status = LoadFrame(Tree, ApfsRead64(Record.Value), FALSE, (UINT16)(Frame->Node.Level - 1),
                         &Result->Frames[Depth + 1]);
      if (EFI_ERROR(Status))
        goto Failure;
    }
  }
  *Cursor = Result;
  return EFI_SUCCESS;

Failure:
  ApfsBtreeClose(Result);
  return Status;
}
