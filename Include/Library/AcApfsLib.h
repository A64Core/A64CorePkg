/** @file
  Read-only APFS container and volume metadata access.
**/

#ifndef AC_APFS_LIB_H
#define AC_APFS_LIB_H

#include <IndustryStandard/Apfs.h>

/** Read Size bytes at a partition-relative byte offset. Success requires a complete read. **/
typedef EFI_STATUS(EFIAPI *AC_APFS_READ)(IN VOID *Context, IN UINT64 Offset, IN UINTN Size,
                                         OUT VOID *Buffer);

typedef struct
{
  VOID        *Context;
  UINT64       Size;
  AC_APFS_READ Read;
} AC_APFS_IO;

typedef struct AC_APFS_CONTAINER AC_APFS_CONTAINER;
typedef struct AC_APFS_VOLUME    AC_APFS_VOLUME;
typedef struct AC_APFS_FILE      AC_APFS_FILE;

typedef enum
{
  AcApfsFileRegular,
  AcApfsFileDirectory,
  AcApfsFileSymlink,
  AcApfsFileOther
} AC_APFS_FILE_TYPE;

typedef struct
{
  UINT64            FileId;
  AC_APFS_FILE_TYPE Type;
  UINT64            Size;
  UINT64            AllocatedSize;
  UINT64            CreateTime;
  UINT64            AccessTime;
  UINT64            ModificationTime;
  UINT32            BsdFlags;
} AC_APFS_FILE_INFO;

typedef struct
{
  UINT64            FileId;
  AC_APFS_FILE_TYPE Type;
  CHAR8             Name[];
} AC_APFS_DIRECTORY_ENTRY;

typedef struct
{
  UINT8  Uuid[16];
  UINT32 BlockSize;
  UINT64 BlockCount;
  UINT64 CheckpointXid;
  UINT32 VolumeCount;
} AC_APFS_CONTAINER_INFO;

typedef struct
{
  UINT8  Uuid[16];
  UINT8  GroupUuid[16];
  UINT16 Role;
  UINT64 Features;
  UINT64 ReadOnlyCompatibleFeatures;
  UINT64 IncompatibleFeatures;
  UINT64 Flags;
  CHAR8  Name[APFS_VOLUME_NAME_SIZE];
} AC_APFS_VOLUME_INFO;

#endif
