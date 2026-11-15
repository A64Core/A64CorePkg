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
  EFI_GUID       Guid;
  CHAR16        *Name;
  AC_CONFIG_DATA Value;
} AC_NVRAM_ADD_ENTRY;

typedef struct
{
  EFI_GUID Guid;
  CHAR16  *Name;
} AC_NVRAM_DELETE_ENTRY;

typedef struct
{
  UINTN                  AddCount;
  AC_NVRAM_ADD_ENTRY    *Add;
  UINTN                  DeleteCount;
  AC_NVRAM_DELETE_ENTRY *Delete;
} AC_NVRAM_CONFIG;

typedef struct
{
  CHAR8         *Name;
  AC_CONFIG_DATA Value;
} AC_ADT_PROPERTY_ENTRY;

typedef struct
{
  CHAR8                 *Path;
  UINTN                  PropertyCount;
  AC_ADT_PROPERTY_ENTRY *Properties;
} AC_ADT_ADD_ENTRY;

typedef struct
{
  CHAR8 *Path;
  CHAR8 *Property;
} AC_ADT_DELETE_ENTRY;

typedef struct
{
  UINTN                AddCount;
  AC_ADT_ADD_ENTRY    *Add;
  UINTN                DeleteCount;
  AC_ADT_DELETE_ENTRY *Delete;
  BOOLEAN              Invalid;
} AC_ADT_CONFIG;

typedef enum
{
  AcFramebufferAuto,
  AcFramebufferOverride,
  AcFramebufferInvalid
} AC_FRAMEBUFFER_MODE;

typedef struct
{
  AC_FRAMEBUFFER_MODE Mode;
  UINT64              Address;
  UINT64              Size;
  UINT32              Width;
  UINT32              Height;
  UINT32              RowBytes;
  UINT32              Rotation;
} AC_FRAMEBUFFER_CONFIG;

typedef enum
{
  AcCpuHandoffDirect,
  AcCpuHandoffSecureLaunch,
  AcCpuHandoffInvalid
} AC_CPU_HANDOFF_MODE;

typedef struct
{
  AC_CPU_HANDOFF_MODE Mode;
  CHAR8              *TcbPath;
} AC_CPU_HANDOFF_CONFIG;

typedef struct
{
  AC_FRAMEBUFFER_CONFIG Framebuffer;
  AC_CPU_HANDOFF_CONFIG CpuHandoff;
} AC_PLATFORM_CONFIG;

typedef struct
{
  AC_UEFI_CONFIG     Uefi;
  AC_KERNEL_CONFIG   Kernel;
  AC_NVRAM_CONFIG    Nvram;
  AC_PLATFORM_CONFIG Platform;
  AC_ADT_CONFIG      Adt;
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

  Kernel.Patch owns its strings and decoded Data. Version bounds are parsed
  from one to three decimal components in 0..99; omitted components are zero.
  Empty bounds mean unrestricted. Counts must fit UINT32. Invalid patch entries
  are diagnosed and disabled in place, retaining their source indices.

  NVRAM owns flattened GUID/name entries and encoded values in source order.
  Data is copied, strings include NUL, integers use four little-endian bytes,
  and booleans use one byte. Invalid entries are diagnosed and skipped.
  ADT.Add maps absolute node paths to property dictionaries using the same value
  encoding. ADT.Delete is an array of Path/Property dictionaries; an omitted
  Property deletes the subtree. Invalid ADT settings mark Adt.Invalid and prevent
  tree preparation. ADT owns all paths, names, and property data.
  Framebuffer defaults to Auto. Override requires Address, Size, Width, Height,
  RowBytes, PixelFormat (BGRX8), and Rotation (0/90/180/270). Invalid framebuffer
  fields leave Mode Invalid so boot preparation cannot silently use Auto.
  CpuHandoff defaults to Direct. SecureLaunch requires an ASCII TCBPath relative to
  EFI\AC on the application's source partition. Both paths enter from EL1;
  the selected mode is executed without hardware probing or fallback.
**/
EFI_STATUS
AcConfigurationInit(IN CONST VOID *Buffer, IN UINTN BufferSize, OUT AC_GLOBAL_CONFIG *Config,
                    OUT UINTN *IssueCount OPTIONAL);

/** Deep-free and clear Config. NULL and an already-cleared config are allowed. **/
VOID AcConfigurationFree(IN OUT AC_GLOBAL_CONFIG *Config OPTIONAL);

#endif
