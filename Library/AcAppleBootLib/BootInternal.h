/** @file
  Private Apple boot memory ownership and helpers.
**/

#ifndef APPLE_BOOT_INTERNAL_H
#define APPLE_BOOT_INTERNAL_H

#include <Library/AcAppleBootLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define AC_BOOT_PAGE_SIZE 0x4000U
#define AC_BOOT_BLOCK_SIZE 0x2000000U
#define AC_BOOT_MAX_DATA_SIZE (16U * 1024U * 1024U)
#define AC_BOOT_MAX_TRUST_CACHES 64U

struct AC_APPLE_BOOT_CONTEXT
{
  AC_APPLE_BOOT_INFO     Info;
  UINTN                  PageCount;
  EFI_MEMORY_DESCRIPTOR *MemoryMap;
  UINTN                  MemoryMapCount;
};

EFI_STATUS InternalBootAllocateResources(UINTN Offset, UINTN Size,
                                         IN OUT AC_APPLE_BOOT_CONTEXT *Context);
EFI_STATUS InternalBootTrustCacheSize(CONST AC_APPLE_BOOT_TRUST_CACHE *Modules, UINTN Count,
                                      OUT UINTN *Size);
VOID       InternalBootWriteTrustCaches(VOID *Buffer, CONST AC_APPLE_BOOT_TRUST_CACHE *Modules,
                                        UINTN Count);

#endif
