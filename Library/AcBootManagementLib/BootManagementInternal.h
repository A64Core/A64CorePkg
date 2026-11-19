/** @file
  A64Core boot management internal interfaces.
**/

#ifndef BOOT_MANAGEMENT_INTERNAL_H
#define BOOT_MANAGEMENT_INTERNAL_H

#include <Library/AcBootManagementLib.h>
#include <Library/AcAppleKernelLib.h>
#include <Library/AcAppleBootLib.h>
#include <Library/AcAppleNvramLib.h>
#include <Library/AcCpuHandoffLib.h>
#include <Library/AcAppleImg4Lib.h>
#include <Library/AcAcpiLib.h>
#include <Guid/FileInfo.h>
#include <IndustryStandard/Apfs.h>
#include <Protocol/AcApfsVolumeInfo.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/PartitionInfo.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseCryptLib.h>
#include <Library/AcPlistLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define AC_BOOT_TICKET_LIMIT (1024U * 1024U)

typedef struct
{
  EFI_HANDLE                      Handle;
  AC_APFS_VOLUME_INFO_PROTOCOL    Identity;
  EFI_GUID                        PartitionType;
  CONST EFI_DEVICE_PATH_PROTOCOL *DiskPath;
  UINTN                           DiskPathSize;
} AC_BOOT_APFS_VOLUME;

extern CONST EFI_GUID gAcBootApfsIscPartitionType;
extern CONST EFI_GUID gAcBootApfsRecoveryPartitionType;

EFI_STATUS InternalCollectBootVolumes(OUT AC_BOOT_APFS_VOLUME **Volumes, OUT UINTN *VolumeCount);

EFI_STATUS InternalAppendBootEntry(CONST AC_BOOT_APFS_VOLUME *Volume, AC_BOOT_ENTRY_TYPE Type,
                                   CONST CHAR8 *Name, CONST CHAR16 *DirectoryPath,
                                   IN OUT AC_BOOT_ENTRY **Entries, IN OUT UINTN *Count);

EFI_STATUS InternalScanRecovery(CONST AC_BOOT_APFS_VOLUME *Volume, IN OUT AC_BOOT_ENTRY **Entries,
                                IN OUT UINTN *Count);

EFI_STATUS InternalResolveRecoveryResources(EFI_HANDLE Source, EFI_FILE_PROTOCOL *Root,
                                            CONST CHAR16          *DirectoryPath,
                                            OUT AC_BOOT_RESOURCES *Resources);

EFI_STATUS InternalGetBootPartition(EFI_HANDLE Handle, OUT EFI_GUID *Type,
                                    OUT CONST EFI_DEVICE_PATH_PROTOCOL **DiskPath,
                                    OUT UINTN                           *DiskPathSize);

typedef enum
{
  AcBootMenuSelect,
  AcBootMenuReboot
} AC_BOOT_MENU_ACTION;

typedef struct
{
  AC_ACPI_MADT_INFO   Madt;
  UINT64              BootMpidr;
  UINT32              CounterFrequency;
  UINTN               EnabledCpuCount;
  AC_APPLE_BOOT_VIDEO Video;
  UINT64              FramebufferSize;
  CHAR8               CommandLine[AC_APPLE_BOOT_LINE_LENGTH];
  UINT8               RandomSeed[256];
} AC_BOOT_PLATFORM;

typedef struct
{
  UINTN                   FileSize;
  UINTN                   BufferSize;
  AC_KERNEL_IMAGE_INFO    Image;
  AC_APPLE_BOOT_CONTEXT  *BootContext;
  AC_CPU_HANDOFF_CONTEXT *CpuContext;
  VOID                   *ExitMap;
  UINTN                   ExitMapCapacity;
  UINT32                  EntryCount;
  UINT32                  DarwinVersion;
  UINTN                   PatchApplied;
  UINTN                   PatchFailed;
  UINTN                   PatchReplacements;
  AC_APPLE_BOOT_VIDEO     Video;
  UINT64                  BootMpidr;
  UINTN                   EnabledCpuCount;
  UINT32                  CounterFrequency;
  CHAR8                   CommandLine[AC_APPLE_BOOT_LINE_LENGTH];
  CONST CHAR16           *Stage;
} AC_BOOT_KERNEL;

EFI_STATUS InternalLoadBootKernel(IN EFI_HANDLE ImageHandle, IN CONST AC_BOOT_ENTRY *Entry,
                                  IN CONST AC_BOOT_RESOURCES *Resources,
                                  IN CONST AC_GLOBAL_CONFIG *Config, OUT AC_BOOT_KERNEL *Kernel);

VOID InternalFreeBootKernel(IN OUT AC_BOOT_KERNEL *Kernel);

EFI_STATUS InternalPrepareBootExit(IN OUT AC_BOOT_KERNEL *Kernel);

EFI_STATUS InternalStartBootKernel(IN EFI_HANDLE ImageHandle, IN OUT AC_BOOT_KERNEL *Kernel);

EFI_STATUS InternalReadBootPlatform(IN CONST AC_FRAMEBUFFER_CONFIG  *Framebuffer,
                                    IN CONST AC_APPLE_NVRAM_CONTEXT *Nvram,
                                    OUT AC_BOOT_PLATFORM *Platform, OUT CONST CHAR16 **Stage);

VOID InternalFreeBootPlatform(IN OUT AC_BOOT_PLATFORM *Platform);

EFI_STATUS InternalCreateBootDeviceTree(IN CONST AC_BOOT_ENTRY       *Entry,
                                        IN CONST AC_BOOT_RESOURCES   *Resources,
                                        IN CONST AC_BOOT_PLATFORM    *Platform,
                                        IN CONST AC_APPLE_NVRAM_INFO *Nvram,
                                        IN CONST AC_ADT_CONFIG *Config, IN BOOLEAN HasTrustCache,
                                        IN AC_CPU_HANDOFF_MODE  HandoffMode,
                                        OUT AC_APPLE_ADT_TREE **Tree);

EFI_STATUS InternalUpdateBootDeviceTree(IN OUT AC_APPLE_ADT_TREE    *Tree,
                                        IN CONST AC_APPLE_BOOT_INFO *Info);

EFI_STATUS
InternalShowBootMenu(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *Input, EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *Output,
                     CONST AC_BOOT_ENTRY *Entries, UINTN EntryCount, BOOLEAN ResetReturned,
                     OUT AC_BOOT_MENU_ACTION *Action, OUT UINTN *SelectedIndex);

EFI_STATUS
InternalShowBootResources(EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *Input,
                          EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *Output, CONST AC_BOOT_ENTRY *Entry,
                          EFI_STATUS ResolveStatus, CONST AC_BOOT_RESOURCES *Resources,
                          EFI_STATUS KernelStatus, CONST AC_BOOT_KERNEL *Kernel);

EFI_STATUS
InternalOpenBootVolume(CONST EFI_DEVICE_PATH_PROTOCOL *DevicePath, UINT16 Role,
                       OUT EFI_FILE_PROTOCOL **Root, OUT EFI_HANDLE *Handle);

EFI_STATUS
InternalOpenBootRoot(CONST AC_BOOT_ENTRY *Entry, OUT EFI_FILE_PROTOCOL **Root,
                     OUT EFI_HANDLE *Handle);

EFI_STATUS
InternalGetBootFileInfo(EFI_FILE_PROTOCOL *File, OUT EFI_FILE_INFO **Info);

EFI_STATUS
InternalReadBootDirectory(EFI_FILE_PROTOCOL *Directory, OUT EFI_FILE_INFO **Info);

EFI_STATUS
InternalReadBootFile(EFI_FILE_PROTOCOL *Root, CONST CHAR16 *Path, UINTN Limit, OUT VOID **Buffer,
                     OUT UINTN *Size);

CHAR16 *InternalJoinBootPath(CONST CHAR16 *Directory, CONST CHAR16 *Name);

BOOLEAN InternalParseBootUuid(CONST CHAR16 *Text, OUT UINT8 Uuid[16]);

EFI_STATUS
InternalResolveBootResources(EFI_FILE_PROTOCOL *Root, CONST CHAR16 *DirectoryPath,
                             OUT AC_BOOT_RESOURCES *Resources);

#endif
