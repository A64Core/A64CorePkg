/** @file
  Apple NVRAM variable capture and V3 boot data.
**/

#ifndef AC_APPLE_NVRAM_LIB_H
#define AC_APPLE_NVRAM_LIB_H

#include <Uefi.h>

typedef struct AC_APPLE_NVRAM_CONTEXT AC_APPLE_NVRAM_CONTEXT;

typedef struct
{
  CONST VOID *Data;
  UINTN       Size;
  UINT32      BankCount;
  UINT32      CurrentBank;
  UINT32      RawController;
} AC_APPLE_NVRAM_INFO;

extern CONST EFI_GUID gAcAppleNvramGuid;
extern CONST EFI_GUID gAcAppleSystemNvramGuid;

/**
  Capture the Apple and Apple System UEFI variable namespaces and encode V3 data.

  Two consecutive enumerations must agree; firmware changes receive bounded
  retries and then EFI_ABORTED. Names use UTF-16 in UEFI and UTF-8 in V3. Typed
  values use one-byte booleans, little-endian UINT32 numbers, and byte strings;
  other values remain binary. Malformed values return EFI_COMPROMISED_DATA.
  V3 retains original GUIDs and supplies System aliases for ARM's redirected
  names when an explicit System value is absent.
  Limits are 4096 variables, 1024 UTF-16 name units including NUL, 1 MiB per
  value, and 4 MiB of captured or encoded data. Limits return EFI_BAD_BUFFER_SIZE.

  Context owns all returned data and is NULL on failure. Capture and release
  require Boot Services. The exported bank is a boot-time view; persistence
  requires a separate kernel NVRAM controller.
**/
EFI_STATUS AcAppleNvramCreate(OUT AC_APPLE_NVRAM_CONTEXT **Context);

VOID AcAppleNvramFree(IN AC_APPLE_NVRAM_CONTEXT *Context OPTIONAL);

/** Return borrowed V3 data and its matching bank description. **/
EFI_STATUS AcAppleNvramGetInfo(IN CONST AC_APPLE_NVRAM_CONTEXT *Context,
                               OUT AC_APPLE_NVRAM_INFO         *Info);

/** Return a borrowed original UEFI value, or EFI_NOT_FOUND. **/
EFI_STATUS AcAppleNvramGetVariable(IN CONST AC_APPLE_NVRAM_CONTEXT *Context,
                                   IN CONST EFI_GUID *Guid, IN CONST CHAR8 *Name,
                                   OUT CONST VOID **Data, OUT UINTN *Size);

#endif
