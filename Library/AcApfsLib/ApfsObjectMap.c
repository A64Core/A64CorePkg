/** @file
  Container and volume Object Map access.
**/

#include "ApfsInternal.h"

EFI_STATUS
ApfsOpenObjectMap(CONST AC_APFS_CONTAINER *Container, UINT64 Oid, BOOLEAN ContainerMap,
                  OUT UINT64 *Root)
{
  APFS_OMAP  Omap;
  APFS_TREE  Tree;
  VOID      *Buffer;
  EFI_STATUS Status;

  *Root = 0;
  Status = ApfsReadObject(Container, Oid, Container->Superblock.BlockSize, &Buffer, NULL);
  if (EFI_ERROR(Status))
    return Status;
  CopyMem(&Omap, Buffer, sizeof(Omap));
  FreePool(Buffer);
  if (Omap.Object.Type != (APFS_OBJECT_PHYSICAL | APFS_OBJECT_OMAP) || Omap.Object.Oid != Oid ||
      Omap.Object.Xid > Container->Superblock.Object.Xid || Omap.Object.Subtype != 0)
    return EFI_COMPROMISED_DATA;
  if (Omap.Flags != (ContainerMap ? APFS_OMAP_MANUALLY_MANAGED : 0) ||
      Omap.TreeType != (APFS_OBJECT_PHYSICAL | APFS_OBJECT_BTREE) || Omap.PendingRevertMin != 0 ||
      Omap.PendingRevertMax != 0)
    return EFI_UNSUPPORTED;
  ZeroMem(&Tree, sizeof(Tree));
  Tree.Container = Container;
  Tree.Root = Omap.TreeOid;
  Tree.Xid = Container->Superblock.Object.Xid;
  Tree.Subtype = APFS_OBJECT_OMAP;
  Tree.Physical = TRUE;
  Status = ApfsBtreeCheckRoot(&Tree);
  if (!EFI_ERROR(Status))
    *Root = Tree.Root;
  return Status;
}

EFI_STATUS
ApfsLookupObject(CONST AC_APFS_CONTAINER *Container, UINT64 Root, UINT64 Xid, UINT64 Oid,
                 OUT APFS_OMAP_KEY *Key, OUT APFS_OMAP_VALUE *Value)
{
  APFS_OMAP_KEY Search;
  APFS_TREE     Tree;
  APFS_CURSOR  *Cursor;
  APFS_RECORD   Record;
  EFI_STATUS    Status;

  ZeroMem(Key, sizeof(*Key));
  ZeroMem(Value, sizeof(*Value));
  ZeroMem(&Tree, sizeof(Tree));
  Tree.Container = Container;
  Tree.Root = Root;
  Tree.Xid = Xid;
  Tree.Subtype = APFS_OBJECT_OMAP;
  Tree.Physical = TRUE;
  Search.Oid = Oid;
  Search.Xid = Xid;
  Status = ApfsBtreeSeek(&Tree, &Search, sizeof(Search), TRUE, &Cursor);
  if (EFI_ERROR(Status))
    return Status;
  Status = ApfsBtreeRecord(Cursor, &Record);
  if (!EFI_ERROR(Status))
  {
    CopyMem(Key, Record.Key, sizeof(*Key));
    CopyMem(Value, Record.Value, sizeof(*Value));
  }
  ApfsBtreeClose(Cursor);
  if (EFI_ERROR(Status))
    return Status;
  if (Key->Oid != Oid || (Value->Flags & APFS_OMAP_VAL_DELETED) != 0)
    return EFI_NOT_FOUND;
  if (Key->Xid == 0 || Value->Size == 0 || Value->Size % Container->Superblock.BlockSize != 0)
    return EFI_COMPROMISED_DATA;
  if ((Value->Flags & ~(APFS_OMAP_VAL_SAVED | APFS_OMAP_VAL_CRYPTO_GENERATION)) != 0)
    return EFI_UNSUPPORTED;
  return EFI_SUCCESS;
}
