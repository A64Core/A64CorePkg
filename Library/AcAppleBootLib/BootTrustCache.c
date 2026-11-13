/** @file
  Trust cache module framing and boot offset-table encoding.
**/

#include "BootInternal.h"

EFI_STATUS
InternalBootTrustCacheSize(CONST AC_APPLE_BOOT_TRUST_CACHE *Modules, UINTN Count, OUT UINTN *Size)
{
  UINTN  Index;
  UINTN  Total;
  UINTN  EntrySize;
  UINT32 Version;
  UINT32 Entries;

  *Size = 0;
  if (Count != 0 && Modules == NULL)
    return EFI_INVALID_PARAMETER;
  if (Count > AC_BOOT_MAX_TRUST_CACHES)
    return EFI_BAD_BUFFER_SIZE;
  if (Count == 0)
    return EFI_SUCCESS;
  Total = sizeof(UINT32) * (1 + Count);
  for (Index = 0; Index < Count; ++Index)
  {
    if (Modules[Index].Data == NULL || (UINTN)Modules[Index].Data > MAX_UINTN - Modules[Index].Size)
      return EFI_INVALID_PARAMETER;
    if (Modules[Index].Size < 24)
      return EFI_COMPROMISED_DATA;
    CopyMem(&Version, Modules[Index].Data, sizeof(Version));
    CopyMem(&Entries, (CONST UINT8 *)Modules[Index].Data + 20, sizeof(Entries));
    if (Version != 1 && Version != 2)
      return EFI_UNSUPPORTED;
    EntrySize = Version == 1 ? 22 : 24;
    if ((Modules[Index].Size - 24) % EntrySize != 0 ||
        Entries != (Modules[Index].Size - 24) / EntrySize)
      return EFI_COMPROMISED_DATA;
    Total = ALIGN_VALUE(Total, sizeof(UINT64));
    if (Modules[Index].Size > AC_BOOT_MAX_DATA_SIZE - Total)
      return EFI_BAD_BUFFER_SIZE;
    Total += Modules[Index].Size;
  }
  *Size = Total;
  return EFI_SUCCESS;
}

VOID
InternalBootWriteTrustCaches(VOID *Buffer, CONST AC_APPLE_BOOT_TRUST_CACHE *Modules, UINTN Count)
{
  UINTN  Index;
  UINTN  Offset = sizeof(UINT32) * (1 + Count);
  UINT32 Value = (UINT32)Count;

  CopyMem(Buffer, &Value, sizeof(Value));
  for (Index = 0; Index < Count; ++Index)
  {
    Offset = ALIGN_VALUE(Offset, sizeof(UINT64));
    Value = (UINT32)Offset;
    CopyMem((UINT8 *)Buffer + sizeof(UINT32) * (1 + Index), &Value, sizeof(Value));
    CopyMem((UINT8 *)Buffer + Offset, Modules[Index].Data, Modules[Index].Size);
    Offset += Modules[Index].Size;
  }
}
