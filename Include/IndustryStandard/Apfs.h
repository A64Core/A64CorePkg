/** @file
  On-disk layouts from the Apple File System Reference (2020-06-22).
**/

#ifndef AC_APFS_H
#define AC_APFS_H

#include <Uefi.h>

#define APFS_NX_MAGIC 0x4253584EU
#define APFS_VOLUME_MAGIC 0x42535041U
#define APFS_MIN_BLOCK_SIZE 4096U
#define APFS_MAX_BLOCK_SIZE 65536U
#define APFS_MAX_VOLUMES 100U
#define APFS_VOLUME_NAME_SIZE 256U
#define APFS_NX_INCOMPAT_VERSION2 0x2ULL
#define APFS_XP_NONCONTIGUOUS 0x80000000U

#define APFS_OBJECT_TYPE_MASK 0x0000FFFFU
#define APFS_OBJECT_PHYSICAL 0x40000000U
#define APFS_OBJECT_EPHEMERAL 0x80000000U
#define APFS_OBJECT_NX_SUPERBLOCK 0x1U
#define APFS_OBJECT_BTREE 0x2U
#define APFS_OBJECT_BTREE_NODE 0x3U
#define APFS_OBJECT_SPACEMAN 0x5U
#define APFS_OBJECT_OMAP 0xBU
#define APFS_OBJECT_CHECKPOINT_MAP 0xCU
#define APFS_OBJECT_VOLUME 0xDU
#define APFS_OBJECT_FSTREE 0xEU
#define APFS_OBJECT_REAPER 0x11U
#define APFS_CHECKPOINT_MAP_LAST 0x1U

#define APFS_BTNODE_ROOT 0x1U
#define APFS_BTNODE_LEAF 0x2U
#define APFS_BTNODE_FIXED_KV 0x4U
#define APFS_BTREE_PHYSICAL 0x10U
#define APFS_BTREE_SEQUENTIAL_INSERT 0x2U
#define APFS_BTREE_KV_NONALIGNED 0x40U
#define APFS_OMAP_MANUALLY_MANAGED 0x1U
#define APFS_OMAP_VAL_DELETED 0x1U
#define APFS_OMAP_VAL_SAVED 0x2U
#define APFS_OMAP_VAL_ENCRYPTED 0x4U
#define APFS_OMAP_VAL_NOHEADER 0x8U
#define APFS_OMAP_VAL_CRYPTO_GENERATION 0x10U

#define APFS_VOLUME_ROLE_SYSTEM 0x1U
#define APFS_VOLUME_ROLE_RECOVERY 0x4U
#define APFS_VOLUME_ROLE_PREBOOT 0x10U
#define APFS_VOLUME_ROLE_INSTALLER 0x20U
#define APFS_VOLUME_ROLE_DATA 0x40U

#define APFS_INCOMPAT_CASE_INSENSITIVE 0x1ULL
#define APFS_INCOMPAT_DATALESS_SNAPS 0x2ULL
#define APFS_INCOMPAT_NORMALIZATION_INSENSITIVE 0x8ULL
#define APFS_FS_UNENCRYPTED 0x1ULL
#define APFS_FS_ONEKEY 0x8ULL
#define APFS_FEATURE_VOLGRP_SYSTEM_INO_SPACE 0x10ULL
#define APFS_ROOT_INODE 2ULL
#define APFS_UNIFIED_ID_SPACE_MARK 0x0800000000000000ULL
#define APFS_ID_MASK 0x0FFFFFFFFFFFFFFFULL
#define APFS_RECORD_INODE 3U
#define APFS_RECORD_XATTR 4U
#define APFS_RECORD_FILE_EXTENT 8U
#define APFS_RECORD_DIRECTORY 9U
#define APFS_NAME_LENGTH_MASK 0x3FFU
#define APFS_EXTENT_LENGTH_MASK 0x00FFFFFFFFFFFFFFULL
#define APFS_INODE_BEING_TRUNCATED 0x80ULL
#define APFS_INODE_IS_SPARSE 0x200ULL
#define APFS_BSD_COMPRESSED 0x20U
#define APFS_BSD_HIDDEN 0x8000U
#define APFS_MODE_TYPE_MASK 0170000U
#define APFS_MODE_DIRECTORY 0040000U
#define APFS_MODE_REGULAR 0100000U
#define APFS_MODE_SYMLINK 0120000U
#define APFS_INODE_EXT_DSTREAM 8U

typedef struct
{
  UINT64 Checksum;
  UINT64 Oid;
  UINT64 Xid;
  UINT32 Type;
  UINT32 Subtype;
} APFS_OBJECT;

typedef struct
{
  APFS_OBJECT Object;
  UINT32      Magic;
  UINT32      BlockSize;
  UINT64      BlockCount;
  UINT64      Features;
  UINT64      ReadOnlyCompatibleFeatures;
  UINT64      IncompatibleFeatures;
  UINT8       Uuid[16];
  UINT64      NextOid;
  UINT64      NextXid;
  UINT32      DescriptorBlocks;
  UINT32      DataBlocks;
  UINT64      DescriptorBase;
  UINT64      DataBase;
  UINT32      DescriptorNext;
  UINT32      DataNext;
  UINT32      DescriptorIndex;
  UINT32      DescriptorLength;
  UINT32      DataIndex;
  UINT32      DataLength;
  UINT64      SpacemanOid;
  UINT64      OmapOid;
  UINT64      ReaperOid;
  UINT32      TestType;
  UINT32      MaxFileSystems;
  UINT64      FileSystemOid[APFS_MAX_VOLUMES];
} APFS_NX_SUPERBLOCK;

typedef struct
{
  UINT32 Type;
  UINT32 Subtype;
  UINT32 Size;
  UINT32 Padding;
  UINT64 FileSystemOid;
  UINT64 Oid;
  UINT64 Address;
} APFS_CHECKPOINT_MAPPING;

typedef struct
{
  APFS_OBJECT Object;
  UINT32      Flags;
  UINT32      Count;
} APFS_CHECKPOINT_MAP;

typedef struct
{
  APFS_OBJECT Object;
  UINT32      Flags;
  UINT32      SnapshotCount;
  UINT32      TreeType;
  UINT32      SnapshotTreeType;
  UINT64      TreeOid;
  UINT64      SnapshotTreeOid;
  UINT64      MostRecentSnapshot;
  UINT64      PendingRevertMin;
  UINT64      PendingRevertMax;
} APFS_OMAP;

typedef struct
{
  UINT64 Oid;
  UINT64 Xid;
} APFS_OMAP_KEY;

typedef struct
{
  UINT32 Flags;
  UINT32 Size;
  UINT64 Address;
} APFS_OMAP_VALUE;

typedef struct
{
  UINT16 Offset;
  UINT16 Length;
} APFS_LOCATION;

typedef struct
{
  APFS_OBJECT   Object;
  UINT16        Flags;
  UINT16        Level;
  UINT32        KeyCount;
  APFS_LOCATION TableSpace;
  APFS_LOCATION FreeSpace;
  APFS_LOCATION KeyFreeList;
  APFS_LOCATION ValueFreeList;
} APFS_BTREE_NODE;

typedef struct
{
  UINT16 KeyOffset;
  UINT16 ValueOffset;
} APFS_KV_OFFSET;

typedef struct
{
  APFS_LOCATION Key;
  APFS_LOCATION Value;
} APFS_KV_LOCATION;

typedef struct
{
  UINT32 Flags;
  UINT32 NodeSize;
  UINT32 KeySize;
  UINT32 ValueSize;
  UINT32 LongestKey;
  UINT32 LongestValue;
  UINT64 KeyCount;
  UINT64 NodeCount;
} APFS_BTREE_INFO;

typedef struct
{
  APFS_OBJECT Object;
  UINT32      Magic;
  UINT32      FileSystemIndex;
  UINT64      Features;
  UINT64      ReadOnlyCompatibleFeatures;
  UINT64      IncompatibleFeatures;
  UINT64      UnmountTime;
  UINT64      ReservedBlocks;
  UINT64      QuotaBlocks;
  UINT64      AllocatedBlocks;
  UINT8       MetadataCrypto[20];
  UINT32      RootTreeType;
  UINT32      ExtentTreeType;
  UINT32      SnapshotTreeType;
  UINT64      OmapOid;
  UINT64      RootTreeOid;
  UINT64      ExtentTreeOid;
  UINT64      SnapshotTreeOid;
  UINT64      RevertToXid;
  UINT64      RevertToSuperblockOid;
  UINT64      NextObjectId;
  UINT64      FileCount;
  UINT64      DirectoryCount;
  UINT64      SymlinkCount;
  UINT64      OtherObjectCount;
  UINT64      SnapshotCount;
  UINT64      TotalBlocksAllocated;
  UINT64      TotalBlocksFreed;
  UINT8       Uuid[16];
  UINT64      LastModifiedTime;
  UINT64      Flags;
  UINT8       ModificationHistory[9 * 48];
  CHAR8       Name[APFS_VOLUME_NAME_SIZE];
  UINT32      NextDocumentId;
  UINT16      Role;
  UINT16      Reserved;
  UINT64      RootToXid;
  UINT64      EncryptionStateOid;
  UINT64      CloneInfoIdEpoch;
  UINT64      CloneInfoXid;
  UINT64      SnapshotMetadataExtensionOid;
  UINT8       GroupUuid[16];
} APFS_VOLUME_SUPERBLOCK;

#pragma pack(push, 1)
typedef struct
{
  UINT64 ParentId;
  UINT64 PrivateId;
  UINT64 CreateTime;
  UINT64 ModificationTime;
  UINT64 ChangeTime;
  UINT64 AccessTime;
  UINT64 InternalFlags;
  UINT32 ChildrenOrLinks;
  UINT32 ProtectionClass;
  UINT32 WriteGeneration;
  UINT32 BsdFlags;
  UINT32 Owner;
  UINT32 Group;
  UINT16 Mode;
  UINT16 Padding;
  UINT64 UncompressedSize;
} APFS_INODE;

typedef struct
{
  UINT64 FileId;
  UINT64 DateAdded;
  UINT16 Flags;
} APFS_DIRECTORY_VALUE;

typedef struct
{
  UINT64 Size;
  UINT64 AllocatedSize;
  UINT64 DefaultCryptoId;
  UINT64 BytesWritten;
  UINT64 BytesRead;
} APFS_DSTREAM;

typedef struct
{
  UINT64 LengthAndFlags;
  UINT64 PhysicalBlock;
  UINT64 CryptoId;
} APFS_FILE_EXTENT;

typedef struct
{
  UINT16 Count;
  UINT16 UsedData;
} APFS_XFIELD_BLOB;

typedef struct
{
  UINT8  Type;
  UINT8  Flags;
  UINT16 Size;
} APFS_XFIELD;
#pragma pack(pop)

STATIC_ASSERT(sizeof(APFS_OBJECT) == 32, "Invalid APFS object layout");
STATIC_ASSERT(sizeof(APFS_NX_SUPERBLOCK) == 0x3D8, "Invalid APFS NX prefix layout");
STATIC_ASSERT(sizeof(APFS_CHECKPOINT_MAPPING) == 40, "Invalid APFS checkpoint mapping layout");
STATIC_ASSERT(sizeof(APFS_CHECKPOINT_MAP) == 40, "Invalid APFS checkpoint map layout");
STATIC_ASSERT(sizeof(APFS_OMAP) == 88, "Invalid APFS object map layout");
STATIC_ASSERT(sizeof(APFS_OMAP_KEY) == 16, "Invalid APFS object map key layout");
STATIC_ASSERT(sizeof(APFS_OMAP_VALUE) == 16, "Invalid APFS object map value layout");
STATIC_ASSERT(sizeof(APFS_BTREE_NODE) == 56, "Invalid APFS B-tree node layout");
STATIC_ASSERT(sizeof(APFS_BTREE_INFO) == 40, "Invalid APFS B-tree info layout");
STATIC_ASSERT(OFFSET_OF(APFS_VOLUME_SUPERBLOCK, Uuid) == 0xF0, "Invalid APFS volume UUID offset");
STATIC_ASSERT(OFFSET_OF(APFS_VOLUME_SUPERBLOCK, Name) == 0x2C0, "Invalid APFS volume name offset");
STATIC_ASSERT(OFFSET_OF(APFS_VOLUME_SUPERBLOCK, Role) == 0x3C4, "Invalid APFS volume role offset");
STATIC_ASSERT(sizeof(APFS_VOLUME_SUPERBLOCK) == 0x400, "Invalid APFS volume prefix layout");
STATIC_ASSERT(sizeof(APFS_INODE) == 92, "Invalid APFS inode layout");
STATIC_ASSERT(sizeof(APFS_DIRECTORY_VALUE) == 18, "Invalid APFS directory value layout");
STATIC_ASSERT(sizeof(APFS_DSTREAM) == 40, "Invalid APFS data stream layout");
STATIC_ASSERT(sizeof(APFS_FILE_EXTENT) == 24, "Invalid APFS file extent layout");

#endif
