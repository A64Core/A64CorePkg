/** @file
  A64Core boot management library interface.
**/

#ifndef AC_BOOT_MANAGEMENT_LIB_H
#define AC_BOOT_MANAGEMENT_LIB_H

#include <Uefi.h>
#include <Protocol/DevicePath.h>
#include <Library/AcConfigurationLib.h>

typedef enum
{
  AcBootEntryMacOS,
  AcBootEntryRecovery
} AC_BOOT_ENTRY_TYPE;

/** An owned target description; no open files or loaded payloads are retained. **/
typedef struct
{
  AC_BOOT_ENTRY_TYPE        Type;
  CHAR16                   *Name;
  EFI_DEVICE_PATH_PROTOCOL *DevicePath;
  CHAR16                   *DirectoryPath;
  UINT8                     ContainerUuid[16];
  UINT8                     VolumeUuid[16];
} AC_BOOT_ENTRY;

/**
  Owned resource paths relative to the entry's volume, except IscDirectoryPath,
  which is relative to IscDevicePath. Recovery-only fields are zero for macOS.
**/
typedef struct
{
  CHAR16                   *BootDirectory;
  CHAR16                   *KernelPath;
  CHAR16                   *TicketPath;
  CHAR16                   *BaseSystemPath;
  CHAR16                   *BaseSystemTrustCachePath;
  CHAR16                   *StaticTrustCachePath;
  CHAR16                   *BaseSystemRootHashPath;
  CHAR16                   *RootHashPath;
  EFI_DEVICE_PATH_PROTOCOL *IscDevicePath;
  CHAR16                   *IscDirectoryPath;
  CHAR8                    *ProductVersion;
  CHAR8                    *ProductBuildVersion;
  CHAR8                    *RestoreVersion;
  UINT8                     BootManifestHash[48];
} AC_BOOT_RESOURCES;

/**
  Scan connected APFS volumes for Preboot systems and independent Recovery sets.

  The caller owns the array and its members; release it with AcFreeBootEntries().
  No targets is success with NULL Entries and zero EntryCount. Unreadable sources
  are diagnosed and skipped. Allocation failure aborts and clears both outputs.
**/
EFI_STATUS
AcScanBootEntries(OUT AC_BOOT_ENTRY **Entries, OUT UINTN *EntryCount);

VOID AcFreeBootEntries(IN AC_BOOT_ENTRY *Entries OPTIONAL, IN UINTN EntryCount);

/**
  Reopen an entry and locate its resources without loading large payloads.

  Resources must be zero-initialized or released before the call. On failure it
  is cleared. For macOS, DirectoryPath names a volume-group directory and its
  active selector is 96 ASCII hexadecimal digits with optional LF or CRLF.
  Recovery DirectoryPath names a specific boot/<hash> directory; its same-disk
  iSC metadata must match uniquely. Missing or invalid resources never select
  another set. Entry and Resources own their allocations independently.
**/
EFI_STATUS
AcResolveBootResources(IN CONST AC_BOOT_ENTRY *Entry, OUT AC_BOOT_RESOURCES *Resources);

VOID AcFreeBootResources(IN OUT AC_BOOT_RESOURCES *Resources OPTIONAL);

/**
  Run the built-in text picker and boot the selected kernel.

  Config is borrowed and must remain allocated and unchanged until return.
  It must come from AcConfigurationInit().

  Successful boot never returns. Errors before the first ExitBootServices call
  can return to the picker. Later errors halt without calling firmware services.

  @retval EFI_INVALID_PARAMETER  ImageHandle or Config is NULL.
  @retval EFI_UNSUPPORTED  Required console interfaces are unavailable.
  @return                 An error from boot discovery or console input/output.
**/
EFI_STATUS
AcRunBootPicker(IN EFI_HANDLE ImageHandle, IN CONST AC_GLOBAL_CONFIG *Config);

#endif
