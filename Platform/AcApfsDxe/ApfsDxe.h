/** @file
  Private APFS driver contexts and protocol adapters.
**/

#ifndef APFS_DXE_H
#define APFS_DXE_H

#include <Uefi.h>
#include <Guid/FileInfo.h>
#include <Guid/FileSystemInfo.h>
#include <Guid/FileSystemVolumeLabelInfo.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DiskIo.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/AcApfsVolumeInfo.h>
#include <Library/AcApfsLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

typedef struct
{
  LIST_ENTRY             Link;
  EFI_HANDLE             Controller;
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  EFI_DISK_IO_PROTOCOL  *DiskIo;
  UINT32                 MediaId;
  UINT64                 Size;
  BOOLEAN                Invalid;
  AC_APFS_CONTAINER     *Apfs;
  AC_APFS_CONTAINER_INFO Info;
  LIST_ENTRY             Volumes;
} APFS_DXE_CONTAINER;

typedef struct
{
  LIST_ENTRY                      Link;
  APFS_DXE_CONTAINER             *Container;
  EFI_HANDLE                      Handle;
  EFI_DEVICE_PATH_PROTOCOL       *DevicePath;
  AC_APFS_VOLUME_INFO_PROTOCOL    Identity;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL FileSystem;
  AC_APFS_VOLUME                 *Apfs;
  CHAR16                         *Label;
  UINTN                           OpenFiles;
  BOOLEAN                         Published;
  BOOLEAN                         ChildOpen;
  BOOLEAN                         FileSystemInstalled;
} APFS_DXE_VOLUME;

typedef struct
{
  EFI_FILE_PROTOCOL        Protocol;
  APFS_DXE_VOLUME         *Volume;
  AC_APFS_FILE            *Apfs;
  AC_APFS_FILE_INFO        Info;
  CHAR8                   *Path;
  UINT64                   Position;
  AC_APFS_DIRECTORY_ENTRY *PendingEntry;
} APFS_DXE_FILE;

extern EFI_DRIVER_BINDING_PROTOCOL gAcApfsDriverBinding;

EFI_STATUS        ApfsDxeMapStatus(EFI_STATUS Status);
EFI_STATUS        ApfsDxeCheckMedia(APFS_DXE_CONTAINER *Container);
EFI_STATUS EFIAPI ApfsDxeReadDisk(VOID *Context, UINT64 Offset, UINTN Size, VOID *Buffer);
EFI_STATUS ApfsDxeCreateVolume(APFS_DXE_CONTAINER *Container, UINT32 Index,
                               EFI_DEVICE_PATH_PROTOCOL *ParentPath, OUT APFS_DXE_VOLUME **Volume);
EFI_STATUS ApfsDxePublishVolume(APFS_DXE_VOLUME *Volume);
EFI_STATUS ApfsDxeRemoveVolume(APFS_DXE_VOLUME *Volume);
VOID       ApfsDxeFreeVolume(APFS_DXE_VOLUME *Volume);
EFI_STATUS EFIAPI ApfsDxeOpenVolume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This,
                                    EFI_FILE_PROTOCOL              **Root);
EFI_STATUS ApfsDxeOpenFile(APFS_DXE_VOLUME *Volume, CHAR8 *Path, OUT EFI_FILE_PROTOCOL **Handle);
EFI_STATUS ApfsDxeUnicodeToPath(CONST CHAR16 *Text, OUT CHAR8 **Path);
EFI_STATUS ApfsDxeUtf8ToUnicode(CONST CHAR8 *Text, OUT CHAR16 **Result);
EFI_STATUS ApfsDxeFillFileInfo(CONST AC_APFS_FILE_INFO *Info, CONST CHAR8 *Name, UINTN *BufferSize,
                               VOID *Buffer);
EFI_STATUS EFIAPI ApfsDxeGetInfo(EFI_FILE_PROTOCOL *This, EFI_GUID *InformationType,
                                 UINTN *BufferSize, VOID *Buffer);
EFI_STATUS EFIAPI AcApfsDxeEntryPoint(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable);
EFI_STATUS EFIAPI AcApfsDxeUnload(EFI_HANDLE ImageHandle);

#endif
