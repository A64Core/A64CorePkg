/** @file
  Read-only APFS volume identity published on volume child handles.
**/

#ifndef AC_APFS_VOLUME_INFO_PROTOCOL_H
#define AC_APFS_VOLUME_INFO_PROTOCOL_H

#include <Uefi.h>

#define AC_APFS_VOLUME_INFO_PROTOCOL_GUID                                                          \
  {0xC3747397, 0x35D5, 0x415F, {0x88, 0xA4, 0xC5, 0x38, 0x6F, 0x35, 0x90, 0x1C}}

#define AC_APFS_VOLUME_INFO_PROTOCOL_REVISION 1ULL
#define AC_APFS_VOLUME_NAME_CAPACITY 256U

/**
  Driver-owned identity for one binding of an APFS container partition.

  UUIDs retain their on-disk byte order, not EFI_GUID field ordering. Name is
  null-terminated UTF-8. ContainerHandle is the parent partition handle.
  Consumers must check Revision and copy values they retain across driver calls.
  This interface does not imply readable contents; test for Simple File System
  on the same handle. All fields are read-only and expire on driver disconnect.
**/
typedef struct
{
  UINT64     Revision;
  EFI_HANDLE ContainerHandle;
  UINT8      ContainerUuid[16];
  UINT8      VolumeUuid[16];
  UINT8      GroupUuid[16];
  UINT16     Role;
  CHAR8      Name[AC_APFS_VOLUME_NAME_CAPACITY];
} AC_APFS_VOLUME_INFO_PROTOCOL;

extern EFI_GUID gAcApfsVolumeInfoProtocolGuid;

#endif
