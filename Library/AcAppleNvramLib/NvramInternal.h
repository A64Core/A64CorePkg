/** @file
  Private variable ownership and Apple NVRAM V3 layout.
**/

#ifndef NVRAM_INTERNAL_H
#define NVRAM_INTERNAL_H

#include <Library/AcAppleNvramLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#define AC_NVRAM_MAX_VARIABLES 4096U
#define AC_NVRAM_MAX_NAME_UNITS 1024U
#define AC_NVRAM_MAX_VALUE (1024U * 1024U)
#define AC_NVRAM_MAX_STORE (4U * 1024U * 1024U)
#define AC_NVRAM_RETRIES 3U
#define AC_NVRAM_V3_SIGNATURE 0x4E565633U
#define AC_NVRAM_V3_VERSION 1U
#define AC_NVRAM_V3_VARIABLE_SIGNATURE 0x55AAU
#define AC_NVRAM_V3_ADDED 0x7FU

#pragma pack(push, 1)
typedef struct
{
  UINT32 Signature;
  UINT32 Size;
  UINT32 Generation;
  UINT8  State;
  UINT8  Flags;
  UINT8  Version;
  UINT8  Reserved;
  UINT32 SystemSize;
  UINT32 CommonSize;
} AC_NVRAM_V3_HEADER;

typedef struct
{
  UINT16 Signature;
  UINT8  State;
  UINT8  Reserved;
  UINT32 Attributes;
  UINT32 NameSize;
  UINT32 DataSize;
  UINT8  Guid[16];
  UINT32 Crc;
} AC_NVRAM_V3_VARIABLE;
#pragma pack(pop)

STATIC_ASSERT(sizeof(AC_NVRAM_V3_HEADER) == 24, "Invalid V3 store header size");
STATIC_ASSERT(sizeof(AC_NVRAM_V3_VARIABLE) == 36, "Invalid V3 variable header size");

typedef struct AC_NVRAM_VARIABLE
{
  struct AC_NVRAM_VARIABLE *Next;
  EFI_GUID                  Guid;
  CHAR8                    *Name;
  VOID                     *Data;
  UINTN                     Size;
  UINT32                    Attributes;
} AC_NVRAM_VARIABLE;

struct AC_APPLE_NVRAM_CONTEXT
{
  AC_NVRAM_VARIABLE  *Variables;
  AC_APPLE_NVRAM_INFO Info;
};

EFI_STATUS InternalNvramEncodeV3(IN OUT AC_APPLE_NVRAM_CONTEXT *Context);

EFI_STATUS InternalNvramValidateV3(IN CONST VOID *Data, IN UINTN Size);

#endif
