/** @file
  Private APFS read and lookup helpers.
**/

#ifndef APFS_INTERNAL_H
#define APFS_INTERNAL_H

#include <Library/AcApfsLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

struct AC_APFS_CONTAINER
{
  AC_APFS_IO         Io;
  APFS_NX_SUPERBLOCK Superblock;
  UINT64             OmapRoot;
  UINT32             VolumeCount;
};

typedef struct
{
  CONST AC_APFS_CONTAINER *Container;
  UINT64                   Root;
  UINT64                   OmapRoot;
  UINT64                   Xid;
  UINT32                   Subtype;
  BOOLEAN                  Physical;
  BOOLEAN                  HashedNames;
} APFS_TREE;

typedef struct APFS_CURSOR APFS_CURSOR;

typedef struct
{
  CONST UINT8 *Key;
  UINTN        KeySize;
  CONST UINT8 *Value;
  UINTN        ValueSize;
} APFS_RECORD;

typedef struct
{
  UINT32 *Points;
  UINTN   Count;
  UINT32  Hash;
} APFS_NAME;

typedef struct
{
  AC_APFS_FILE_INFO Info;
  UINT64            ParentId;
  UINT64            StreamId;
  UINT64            InternalFlags;
  UINT64            CryptoId;
  UINT32            BsdFlags;
} APFS_INODE_INFO;

struct AC_APFS_VOLUME
{
  CONST AC_APFS_CONTAINER *Container;
  APFS_TREE                Tree;
  UINT64                   RootId;
  UINT64                   IncompatibleFeatures;
};

struct AC_APFS_FILE
{
  AC_APFS_VOLUME *Volume;
  APFS_INODE_INFO Inode;
  APFS_CURSOR    *DirectoryCursor;
  BOOLEAN         AdvanceDirectory;
  BOOLEAN         DirectoryEnd;
};

UINT16 ApfsRead16(CONST VOID *Buffer);
UINT32 ApfsRead32(CONST VOID *Buffer);
UINT64 ApfsRead64(CONST VOID *Buffer);

EFI_STATUS
ApfsReadBytes(CONST AC_APFS_CONTAINER *Container, UINT64 Offset, UINTN Size, VOID *Buffer);

EFI_STATUS
ApfsReadObject(IN CONST AC_APFS_CONTAINER *Container, IN UINT64 Address, IN UINT32 Size,
               OUT VOID **Buffer, OUT BOOLEAN *ReadFailed OPTIONAL);

BOOLEAN
ApfsObjectValid(IN CONST VOID *Buffer, IN UINT32 Size);

EFI_STATUS
ApfsOpenObjectMap(CONST AC_APFS_CONTAINER *Container, UINT64 Oid, BOOLEAN ContainerMap,
                  OUT UINT64 *Root);

EFI_STATUS
ApfsLookupObject(CONST AC_APFS_CONTAINER *Container, UINT64 Root, UINT64 Xid, UINT64 Oid,
                 OUT APFS_OMAP_KEY *Key, OUT APFS_OMAP_VALUE *Value);

EFI_STATUS
ApfsBtreeCheckRoot(CONST APFS_TREE *Tree);

// Floor selects the greatest key <= Search; otherwise select the least key >= Search.
EFI_STATUS
ApfsBtreeSeek(CONST APFS_TREE *Tree, CONST VOID *Search, UINTN SearchSize, BOOLEAN Floor,
              OUT APFS_CURSOR **Cursor);

EFI_STATUS ApfsBtreeNext(APFS_CURSOR *Cursor);
EFI_STATUS ApfsBtreeRecord(CONST APFS_CURSOR *Cursor, OUT APFS_RECORD *Record);
VOID       ApfsBtreeClose(APFS_CURSOR *Cursor OPTIONAL);

EFI_STATUS
ApfsReadVolumeSuperblock(CONST AC_APFS_CONTAINER *Container, UINT32 Index,
                         OUT APFS_VOLUME_SUPERBLOCK *Volume);

EFI_STATUS
ApfsReadInode(AC_APFS_VOLUME *Volume, UINT64 Id, OUT APFS_INODE_INFO *Info);

EFI_STATUS
ApfsResolvePath(AC_APFS_VOLUME *Volume, CONST CHAR8 *Path, OUT APFS_INODE_INFO *Info);

EFI_STATUS
ApfsNormalizeName(CONST CHAR8 *Bytes, UINTN Length, BOOLEAN Normalize, BOOLEAN Fold,
                  OUT APFS_NAME *Name);
VOID ApfsFreeName(APFS_NAME *Name);
EFI_STATUS
ApfsDirectoryKey(CONST APFS_RECORD *Record, BOOLEAN Hashed, OUT CONST CHAR8 **Name,
                 OUT UINTN *Length, OUT UINT32 *Hash);

// Normalized is owned by the caller on success; Name borrows the record buffer.
EFI_STATUS
ApfsParseDirectoryEntry(AC_APFS_VOLUME *Volume, CONST APFS_RECORD *Record, OUT CONST CHAR8 **Name,
                        OUT UINTN *Length, OUT APFS_DIRECTORY_VALUE *Value,
                        OUT APFS_NAME *Normalized);

EFI_STATUS
ApfsValidateXfields(CONST UINT8 *Buffer, UINTN Size, OUT APFS_DSTREAM *Stream OPTIONAL,
                    OUT BOOLEAN *HasStream OPTIONAL);

#endif
