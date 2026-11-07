/** @file
  Symbol-based and masked binary patching for kernel collection images.
**/

#include "AppleKernelInternal.h"

#define AC_MACH_N_STAB 0xE0U
#define AC_MACH_N_TYPE 0x0EU
#define AC_MACH_N_UNDF 0x00U
#define AC_MACH_N_ABS 0x02U
#define AC_MACH_N_SECT 0x0EU

typedef struct
{
  UINT32 Skip;
  UINT32 AppliedCount;
} AC_PATCH_PROGRESS;

STATIC
BOOLEAN
IsOwnedRange(IN CONST AC_PATCHER_CONTEXT *Patcher, IN UINTN Offset, IN UINTN Size)
{
  UINTN   Index;
  UINTN   End;
  BOOLEAN Found;

  if (Offset < Patcher->HeaderOffset)
    return FALSE;

  Found = FALSE;
  for (Index = 0; Index < Patcher->RangeCount; ++Index)
  {
    End = Patcher->Ranges[Index].Offset + Patcher->Ranges[Index].Size;
    if ((Offset >= Patcher->Ranges[Index].Offset) && (Offset < End) && (Size <= End - Offset))
    {
      Found = TRUE;
      break;
    }
  }

  if (!Found)
    return FALSE;

  for (Index = 0; Index < Patcher->ExcludedCount; ++Index)
  {
    End = Patcher->Excluded[Index].Offset + Patcher->Excluded[Index].Size;
    if ((Offset < End) && (Patcher->Excluded[Index].Offset < Offset + Size))
      return FALSE;
  }

  return TRUE;
}

STATIC
EFI_STATUS
FindSymbol(IN CONST AC_PATCHER_CONTEXT *Patcher, IN CONST CHAR8 *Name, OUT UINTN *Offset)
{
  AC_MACH_SYMBOL Symbol;
  CONST CHAR8   *SymbolName;
  UINT32         Index;
  UINT32         StringEnd;
  UINTN          Value;
  UINTN          Resolved;
  EFI_STATUS     Status;
  BOOLEAN        Found;
  BOOLEAN        Unsupported;

  if (!Patcher->HasSymtab)
    return EFI_NOT_FOUND;

  Found = FALSE;
  Unsupported = FALSE;
  Resolved = 0;
  for (Index = 0; Index < Patcher->Symtab.SymbolCount; ++Index)
  {
    CopyMem(&Symbol, Patcher->Buffer + Patcher->Symtab.SymbolsOffset + Index * sizeof(Symbol),
            sizeof(Symbol));
    if ((Symbol.Type & AC_MACH_N_STAB) != 0)
      continue;

    if (Symbol.StringIndex >= Patcher->Symtab.StringsSize)
      return EFI_COMPROMISED_DATA;

    SymbolName =
      (CONST CHAR8 *)(Patcher->Buffer + Patcher->Symtab.StringsOffset + Symbol.StringIndex);
    for (StringEnd = Symbol.StringIndex; StringEnd < Patcher->Symtab.StringsSize; ++StringEnd)
    {
      if (Patcher->Buffer[(UINTN)Patcher->Symtab.StringsOffset + StringEnd] == '\0')
        break;
    }

    if (StringEnd == Patcher->Symtab.StringsSize)
      return EFI_COMPROMISED_DATA;

    if (AsciiStrCmp(SymbolName, Name) != 0)
      continue;

    if ((Symbol.Type & AC_MACH_N_TYPE) == AC_MACH_N_UNDF)
      continue;

    if (((Symbol.Type & AC_MACH_N_TYPE) != AC_MACH_N_SECT) &&
        ((Symbol.Type & AC_MACH_N_TYPE) != AC_MACH_N_ABS))
    {
      Unsupported = TRUE;
      continue;
    }

    Status = InternalPatcherAddressToOffset(Patcher, Symbol.Value, &Value);
    if (Status == EFI_NOT_FOUND)
      continue;
    if (EFI_ERROR(Status))
      return Status;

    if (!IsOwnedRange(Patcher, Value, 1))
    {
      Unsupported = TRUE;
      continue;
    }

    if (Found && (Resolved != Value))
      return EFI_COMPROMISED_DATA;

    Resolved = Value;
    Found = TRUE;
  }

  if (!Found)
    return Unsupported ? EFI_UNSUPPORTED : EFI_NOT_FOUND;

  *Offset = Resolved;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
PatchSpan(IN UINT8 *Buffer, IN UINTN Start, IN UINTN End, IN CONST AC_KERNEL_PATCH *Patch,
          IN OUT AC_PATCH_PROGRESS *Progress)
{
  UINTN  Offset;
  UINT32 Index;
  UINT8  Value;

  if (Patch->Size > End - Start)
    return EFI_SUCCESS;

  Offset = Start;
  while (Offset <= End - Patch->Size)
  {
    for (Index = 0; Index < Patch->Size; ++Index)
    {
      Value = Buffer[Offset + Index];
      if (Patch->Mask != NULL)
        Value &= Patch->Mask[Index];

      if (Value != Patch->Find[Index])
        break;
    }

    if (Index != Patch->Size)
    {
      ++Offset;
      continue;
    }

    if (Progress->Skip > 0)
      --Progress->Skip;
    else
    {
      if (Progress->AppliedCount == MAX_UINT32)
        return EFI_UNSUPPORTED;

      if (Patch->ReplaceMask == NULL)
        CopyMem(Buffer + Offset, Patch->Replace, Patch->Size);
      else
      {
        for (Index = 0; Index < Patch->Size; ++Index)
          Buffer[Offset + Index] =
            ((UINT32)Buffer[Offset + Index] & ~(UINT32)Patch->ReplaceMask[Index]) |
            ((UINT32)Patch->Replace[Index] & (UINT32)Patch->ReplaceMask[Index]);
      }

      ++Progress->AppliedCount;
      if ((Patch->Count != 0) && (Progress->AppliedCount == Patch->Count))
        return EFI_SUCCESS;
    }

    // Both skipped and replaced matches consume the complete pattern.
    Offset += Patch->Size;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
InternalApplyKernelPatch(IN CONST AC_PATCHER_CONTEXT *Patcher, IN CONST AC_KERNEL_PATCH *Patch,
                         OUT UINT32 *AppliedCount)
{
  EFI_STATUS        Status;
  UINTN             Start;
  UINTN             End;
  UINTN             Cursor;
  UINTN             RangeEnd;
  UINTN             Index;
  UINTN             ExcludedIndex;
  UINTN             BlockStart;
  UINTN             BlockEnd;
  AC_PATCH_PROGRESS Progress;

  *AppliedCount = 0;
  Start = Patcher->HeaderOffset;
  if ((Patch->Base != NULL) && (Patch->Base[0] != '\0'))
  {
    Status = FindSymbol(Patcher, Patch->Base, &Start);
    if (EFI_ERROR(Status))
      return Status;
  }

  if (Patch->Find == NULL)
  {
    if (!IsOwnedRange(Patcher, Start, Patch->Size))
      return EFI_UNSUPPORTED;

    CopyMem(Patcher->Buffer + Start, Patch->Replace, Patch->Size);
    *AppliedCount = 1;
    return EFI_SUCCESS;
  }

  End =
    Patcher->Ranges[Patcher->RangeCount - 1].Offset + Patcher->Ranges[Patcher->RangeCount - 1].Size;
  if (Start >= End)
    return EFI_NOT_FOUND;

  if ((Patch->Limit != 0) && (Patch->Limit < End - Start))
    End = Start + Patch->Limit;

  Progress.Skip = Patch->Skip;
  Progress.AppliedCount = 0;
  for (Index = 0; Index < Patcher->RangeCount; ++Index)
  {
    Cursor = MAX(Start, Patcher->Ranges[Index].Offset);
    RangeEnd = MIN(End, Patcher->Ranges[Index].Offset + Patcher->Ranges[Index].Size);
    if (Cursor >= RangeEnd)
      continue;

    for (ExcludedIndex = 0; ExcludedIndex < Patcher->ExcludedCount; ++ExcludedIndex)
    {
      BlockStart = Patcher->Excluded[ExcludedIndex].Offset;
      BlockEnd = BlockStart + Patcher->Excluded[ExcludedIndex].Size;
      if (BlockEnd <= Cursor)
        continue;
      if (BlockStart >= RangeEnd)
        break;

      if (Cursor < BlockStart)
      {
        Status = PatchSpan(Patcher->Buffer, Cursor, BlockStart, Patch, &Progress);
        if (EFI_ERROR(Status) || ((Patch->Count != 0) && (Progress.AppliedCount == Patch->Count)))
          goto Done;
      }

      Cursor = MAX(Cursor, BlockEnd);
      if (Cursor >= RangeEnd)
        break;
    }

    if (Cursor < RangeEnd)
    {
      Status = PatchSpan(Patcher->Buffer, Cursor, RangeEnd, Patch, &Progress);
      if (EFI_ERROR(Status) || ((Patch->Count != 0) && (Progress.AppliedCount == Patch->Count)))
        goto Done;
    }
  }

  Status = EFI_SUCCESS;
Done:
  *AppliedCount = Progress.AppliedCount;
  if (EFI_ERROR(Status))
    return Status;

  return *AppliedCount != 0 ? EFI_SUCCESS : EFI_NOT_FOUND;
}
