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

/**
  Select a valid checkpoint and open its container object map.

  Io is copied; its Context and source remain borrowed and must stay unchanged
  until CloseContainer. The source begins at the APFS partition, not the disk.
  Container is NULL on failure. Allocations require Boot Services in UEFI.

  @retval EFI_UNSUPPORTED       Not APFS, or a required format feature is unsupported.
  @retval EFI_COMPROMISED_DATA  APFS metadata is malformed or no valid checkpoint exists.
  @retval EFI_OUT_OF_RESOURCES An allocation failed.

  Invalid arguments return EFI_INVALID_PARAMETER. Read errors are propagated.
  Fragmented checkpoint areas and Fusion containers are currently unsupported.
**/
EFI_STATUS
AcApfsOpenContainer(IN CONST AC_APFS_IO *Io, OUT AC_APFS_CONTAINER **Container);

/** Free the context without releasing the borrowed source. NULL is allowed. **/
VOID AcApfsCloseContainer(IN AC_APFS_CONTAINER *Container OPTIONAL);

/** Copy container metadata into caller-owned storage; clear Info on failure. **/
EFI_STATUS
AcApfsGetContainerInfo(IN CONST AC_APFS_CONTAINER *Container, OUT AC_APFS_CONTAINER_INFO *Info);

/**
  Read volume metadata by dense index in the container's nonzero volume OID list.

  Info is caller-owned and cleared on failure; an index past the end returns
  EFI_NOT_FOUND. Names are null-terminated UTF-8. This enumerates superblocks,
  including encrypted or sealed volumes; it does not open their file trees or
  imply support for their features. No separately allocated strings are returned.
**/
EFI_STATUS
AcApfsGetVolumeInfo(IN CONST AC_APFS_CONTAINER *Container, IN UINT32 Index,
                    OUT AC_APFS_VOLUME_INFO *Info);

#endif
