/** @file
  A64Core configuration model and ownership.
**/

#ifndef AC_CONFIGURATION_LIB_H
#define AC_CONFIGURATION_LIB_H

#include <Uefi.h>

typedef struct
{
  CHAR8  *Path;
  BOOLEAN Enabled;
  BOOLEAN LoadEarly;
  CHAR8  *Arguments;
  CHAR8  *Comment;
} AC_UEFI_DRIVER_ENTRY;

typedef struct
{
  UINTN                 DriverCount;
  AC_UEFI_DRIVER_ENTRY *Drivers;
} AC_UEFI_CONFIG;

typedef struct
{
  UINT8 *Data;
  UINTN  Size;
} AC_CONFIG_DATA;

typedef struct
{
  BOOLEAN        Enabled;
  CHAR8         *Identifier;
  CHAR8         *Base;
  CHAR8         *Comment;
  AC_CONFIG_DATA Find;
  AC_CONFIG_DATA Replace;
  AC_CONFIG_DATA Mask;
  AC_CONFIG_DATA ReplaceMask;
  UINT32         Count;
  UINT32         Skip;
  UINT32         Limit;
  UINT32         MinKernel;
  UINT32         MaxKernel;
} AC_KERNEL_PATCH_ENTRY;

/** Owned patches in source order; version bounds use Major*10000+Minor*100+Revision. **/
typedef struct
{
  UINTN                  PatchCount;
  AC_KERNEL_PATCH_ENTRY *Patches;
} AC_KERNEL_CONFIG;

typedef struct
{
  AC_UEFI_CONFIG   Uefi;
  AC_KERNEL_CONFIG Kernel;
} AC_GLOBAL_CONFIG;

/**
  Build an owned configuration from a borrowed XML plist buffer.

  Config must be zero-initialized or previously freed. Strings are UTF-8;
  missing and empty strings are NULL. Booleans default to FALSE. Driver paths
  are relative to EFI\AC\Drivers. Entries retain their source order.

  Missing optional fields use defaults. Unknown keys and wrong field types
  increment IssueCount without failing initialization. Non-dictionary driver
  entries and enabled entries with invalid paths are diagnosed and skipped.
  Keys starting with '#' are ignored. IssueCount is reset on entry.

  Malformed XML, duplicate keys and a non-dictionary root are fatal. Allocation
  failure is fatal. On failure Config is cleared; no partial configuration is
  returned. Buffer can be released as soon as this function returns.

**/
EFI_STATUS
AcConfigurationInit(IN CONST VOID *Buffer, IN UINTN BufferSize, OUT AC_GLOBAL_CONFIG *Config,
                    OUT UINTN *IssueCount OPTIONAL);

/** Deep-free and clear Config. NULL and an already-cleared config are allowed. **/
VOID AcConfigurationFree(IN OUT AC_GLOBAL_CONFIG *Config OPTIONAL);

#endif
