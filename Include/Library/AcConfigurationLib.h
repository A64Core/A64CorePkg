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
  AC_UEFI_CONFIG Uefi;
} AC_GLOBAL_CONFIG;

EFI_STATUS
AcConfigurationInit(IN CONST VOID *Buffer, IN UINTN BufferSize, OUT AC_GLOBAL_CONFIG *Config,
                    OUT UINTN *IssueCount OPTIONAL);

VOID AcConfigurationFree(IN OUT AC_GLOBAL_CONFIG *Config OPTIONAL);

#endif
