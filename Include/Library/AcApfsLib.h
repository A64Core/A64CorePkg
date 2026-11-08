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

/**
  Open an unencrypted, unsealed volume's current file-system tree.

  Volume borrows Container. Close all files before the volume and all volumes
  before the container. Sources must remain unchanged throughout. Unknown
  required features, encrypted or sealed volumes, and restore/revert states
  are unsupported. Existing snapshots do not prevent reading the current tree;
  selecting a snapshot is not supported. Output is NULL on failure.
**/
EFI_STATUS
AcApfsOpenVolume(IN CONST AC_APFS_CONTAINER *Container, IN UINT32 Index,
                 OUT AC_APFS_VOLUME **Volume);

VOID AcApfsCloseVolume(IN AC_APFS_VOLUME *Volume OPTIONAL);

/**
  Open a file or directory by absolute UTF-8 path. "/" opens the root.

  Repeated separators, ".", and ".." are accepted; ".." at the root stays there.
  A trailing separator requires a directory. Symbolic links, compressed files,
  and other special files return EFI_UNSUPPORTED. File borrows Volume and is
  NULL on failure. Invalid UTF-8 paths return EFI_INVALID_PARAMETER; valid
  codepoints beyond the supported Unicode 9.0 repertoire return EFI_UNSUPPORTED.
**/
EFI_STATUS
AcApfsOpenFile(IN AC_APFS_VOLUME *Volume, IN CONST CHAR8 *Path, OUT AC_APFS_FILE **File);

VOID AcApfsCloseFile(IN AC_APFS_FILE *File OPTIONAL);

EFI_STATUS
AcApfsGetFileInfo(IN CONST AC_APFS_FILE *File, OUT AC_APFS_FILE_INFO *Info);

/**
  Read metadata for an ordinary file or directory without opening its contents.

  Times are nanoseconds since the Unix epoch. Info is caller-owned and cleared
  on failure. Missing IDs return EFI_NOT_FOUND; unsupported file types,
  compressed files, and encrypted streams return EFI_UNSUPPORTED.
**/
EFI_STATUS
AcApfsGetFileInfoById(IN AC_APFS_VOLUME *Volume, IN UINT64 FileId, OUT AC_APFS_FILE_INFO *Info);

/**
  Read ordinary file data at a byte offset into caller-owned storage.

  BufferSize is the requested length on input and bytes read on output, including
  on error. EOF returns success with a short or zero read. A NULL Buffer is valid
  only for a zero request. On error, bytes outside the reported prefix may have
  changed. Reading a directory returns EFI_UNSUPPORTED. No file position is kept.
**/
EFI_STATUS
AcApfsReadFile(IN AC_APFS_FILE *File, IN UINT64 Offset, IN OUT UINTN *BufferSize,
               OUT VOID *Buffer OPTIONAL);

/**
  Return the next directory entry in on-disk order, with its original UTF-8 name.

  BufferSize includes the fixed prefix and terminating NUL. EFI_BUFFER_TOO_SMALL
  reports the required size without consuming an entry; NULL Entry is permitted
  for that query. EFI_NOT_FOUND means end of directory and sets BufferSize to zero.
  Each directory handle owns an independent cursor. Errors do not consume entries.
**/
EFI_STATUS
AcApfsReadDirectory(IN AC_APFS_FILE *Directory, IN OUT UINTN *BufferSize,
                    OUT AC_APFS_DIRECTORY_ENTRY *Entry OPTIONAL);

EFI_STATUS
AcApfsRewindDirectory(IN AC_APFS_FILE *Directory);

#endif
