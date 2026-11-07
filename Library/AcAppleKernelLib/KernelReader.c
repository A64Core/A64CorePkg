/** @file
  Unpacked AArch64 Mach-O payload inspection.
**/

#include "AppleKernelInternal.h"

EFI_STATUS
InternalReadMachHeader(IN CONST UINT8 *Buffer, IN UINTN BufferSize, IN UINTN HeaderOffset,
                       OUT AC_MACH_HEADER *Header)
{
  UINT32          Magic;
  UINT32          Index;
  UINTN           Offset;
  UINTN           End;
  AC_MACH_COMMAND Command;
  AC_MACH_SEGMENT Segment;
  AC_MACH_SYMTAB  Symtab;
  BOOLEAN         HasSymtab;

  if ((HeaderOffset > BufferSize) || (BufferSize - HeaderOffset < sizeof(Magic)))
    return EFI_COMPROMISED_DATA;

  CopyMem(&Magic, Buffer + HeaderOffset, sizeof(Magic));
  if (Magic != AC_MACH_MAGIC_64)
    return EFI_UNSUPPORTED;

  if (BufferSize - HeaderOffset < sizeof(*Header))
    return EFI_COMPROMISED_DATA;

  CopyMem(Header, Buffer + HeaderOffset, sizeof(*Header));
  if ((Header->CpuType != AC_MACH_CPU_ARM64) ||
      ((Header->FileType != AC_MACH_EXECUTE) && (Header->FileType != AC_MACH_KEXT_BUNDLE) &&
       (Header->FileType != AC_MACH_FILESET)))
    return EFI_UNSUPPORTED;

  Offset = HeaderOffset + sizeof(*Header);
  if ((Header->CommandsSize > BufferSize - Offset) ||
      (Header->CommandCount > Header->CommandsSize / sizeof(Command)))
    return EFI_COMPROMISED_DATA;

  End = Offset + Header->CommandsSize;
  HasSymtab = FALSE;
  for (Index = 0; Index < Header->CommandCount; ++Index)
  {
    if (End - Offset < sizeof(Command))
      return EFI_COMPROMISED_DATA;

    CopyMem(&Command, Buffer + Offset, sizeof(Command));
    if ((Command.Size < sizeof(Command)) || ((Command.Size & 7U) != 0) ||
        (Command.Size > End - Offset))
      return EFI_COMPROMISED_DATA;

    if (Command.Type == AC_MACH_LC_SEGMENT_64)
    {
      if (Command.Size < sizeof(Segment))
        return EFI_COMPROMISED_DATA;

      CopyMem(&Segment, Buffer + Offset, sizeof(Segment));
      if ((Segment.SectionCount > (Command.Size - sizeof(Segment)) / AC_MACH_SECTION_64_SIZE) ||
          (Segment.FileOffset > BufferSize) ||
          (Segment.FileSize > BufferSize - Segment.FileOffset) ||
          (Segment.VirtualSize > MAX_UINT64 - Segment.VirtualAddress))
        return EFI_COMPROMISED_DATA;
    }
    else if (Command.Type == AC_MACH_LC_SYMTAB)
    {
      if (HasSymtab || (Command.Size != sizeof(Symtab)))
        return EFI_COMPROMISED_DATA;

      CopyMem(&Symtab, Buffer + Offset, sizeof(Symtab));
      if ((Symtab.SymbolsOffset > BufferSize) ||
          (Symtab.SymbolCount > (BufferSize - Symtab.SymbolsOffset) / sizeof(AC_MACH_SYMBOL)) ||
          (Symtab.StringsOffset > BufferSize) ||
          (Symtab.StringsSize > BufferSize - Symtab.StringsOffset))
        return EFI_COMPROMISED_DATA;

      HasSymtab = TRUE;
    }

    Offset += Command.Size;
  }

  return Offset == End ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
}

EFI_STATUS
AcKernelIdentifyPayload(IN CONST VOID *Buffer, IN UINTN BufferSize,
                        OUT AC_KERNEL_PAYLOAD_TYPE *Type)
{
  EFI_STATUS     Status;
  AC_MACH_HEADER Header;

  if (Type == NULL)
    return EFI_INVALID_PARAMETER;

  *Type = AcKernelPayloadUnknown;
  if (Buffer == NULL)
    return EFI_INVALID_PARAMETER;

  Status = InternalReadMachHeader(Buffer, BufferSize, 0, &Header);
  if (EFI_ERROR(Status))
    return Status;

  if (Header.FileType == AC_MACH_FILESET)
    *Type = AcKernelPayloadCollection;
  else if (Header.FileType == AC_MACH_EXECUTE)
    *Type = AcKernelPayloadMachO;
  else
    return EFI_UNSUPPORTED;

  return EFI_SUCCESS;
}

STATIC
VOID
SortAndMergeRanges(IN OUT AC_KERNEL_FILE_RANGE *Ranges, IN OUT UINTN *Count)
{
  AC_KERNEL_FILE_RANGE Value;
  UINTN                Index;
  UINTN                Previous;
  UINTN                Used;
  UINTN                End;

  for (Index = 1; Index < *Count; ++Index)
  {
    Value = Ranges[Index];
    Previous = Index;
    while ((Previous > 0) && (Ranges[Previous - 1].Offset > Value.Offset))
    {
      Ranges[Previous] = Ranges[Previous - 1];
      --Previous;
    }

    Ranges[Previous] = Value;
  }

  Used = 0;
  for (Index = 0; Index < *Count; ++Index)
  {
    if (Ranges[Index].Size == 0)
      continue;

    if ((Used > 0) && (Ranges[Index].Offset <= Ranges[Used - 1].Offset + Ranges[Used - 1].Size))
    {
      End = MAX(Ranges[Used - 1].Offset + Ranges[Used - 1].Size,
                Ranges[Index].Offset + Ranges[Index].Size);
      Ranges[Used - 1].Size = End - Ranges[Used - 1].Offset;
    }
    else
      Ranges[Used++] = Ranges[Index];
  }

  *Count = Used;
}

STATIC
UINT32
ReadImageCommands(IN CONST UINT8 *Buffer, IN UINTN HeaderOffset, IN CONST AC_MACH_HEADER *Header,
                  OUT AC_MACH_SEGMENT *Segments OPTIONAL, OUT AC_KERNEL_FILE_RANGE *Ranges OPTIONAL,
                  IN OUT UINTN *RangeCount, OUT AC_MACH_SYMTAB *Symtab OPTIONAL,
                  OUT BOOLEAN *HasSymtab OPTIONAL)
{
  AC_MACH_COMMAND Command;
  AC_MACH_SEGMENT Segment;
  UINTN           Offset;
  UINTN           End;
  UINT32          SegmentIndex;

  Offset = HeaderOffset + sizeof(*Header);
  End = Offset + Header->CommandsSize;
  SegmentIndex = 0;
  if (Ranges != NULL)
  {
    Ranges[*RangeCount].Offset = HeaderOffset;
    Ranges[*RangeCount].Size = End - HeaderOffset;
  }

  ++*RangeCount;
  for (; Offset < End; Offset += Command.Size)
  {
    CopyMem(&Command, Buffer + Offset, sizeof(Command));
    if (Command.Type == AC_MACH_LC_SEGMENT_64)
    {
      CopyMem(&Segment, Buffer + Offset, sizeof(Segment));
      if (Segments != NULL)
        Segments[SegmentIndex] = Segment;

      ++SegmentIndex;
      if (Segment.FileSize != 0)
      {
        if (Ranges != NULL)
        {
          Ranges[*RangeCount].Offset = (UINTN)Segment.FileOffset;
          Ranges[*RangeCount].Size = (UINTN)Segment.FileSize;
        }

        ++*RangeCount;
      }
    }
    else if ((Command.Type == AC_MACH_LC_SYMTAB) && (Symtab != NULL) && (HasSymtab != NULL))
    {
      CopyMem(Symtab, Buffer + Offset, sizeof(*Symtab));
      *HasSymtab = TRUE;
    }
  }

  return SegmentIndex;
}

EFI_STATUS
InternalInitPatcher(IN CONST AC_KERNEL_COLLECTION_CONTEXT *Collection, IN UINT32 EntryIndex,
                    OUT AC_PATCHER_CONTEXT *Patcher)
{
  EFI_STATUS     Status;
  AC_MACH_HEADER Header;
  AC_MACH_HEADER Root;
  UINT32         Index;
  UINTN          Count;
  UINTN          Total;

  ZeroMem(Patcher, sizeof(*Patcher));
  Patcher->Buffer = Collection->Buffer;
  Patcher->HeaderOffset = Collection->Entries[EntryIndex].FileOffset;
  Status = InternalReadMachHeader(Collection->Buffer, Collection->BufferSize, Patcher->HeaderOffset,
                                  &Header);
  if (EFI_ERROR(Status))
    return Status;

  // CommandCount bounds the number of segments, including zero-file-size ones.
  Patcher->Segments = AllocateZeroPool(MAX(1U, Header.CommandCount) * sizeof(*Patcher->Segments));
  Count = 0;
  ReadImageCommands(Collection->Buffer, Patcher->HeaderOffset, &Header, NULL, NULL, &Count, NULL,
                    NULL);
  Patcher->Ranges = AllocateZeroPool(Count * sizeof(*Patcher->Ranges));
  if ((Patcher->Segments == NULL) || (Patcher->Ranges == NULL))
  {
    Status = EFI_OUT_OF_RESOURCES;
    goto Error;
  }

  Patcher->SegmentCount =
    ReadImageCommands(Collection->Buffer, Patcher->HeaderOffset, &Header, Patcher->Segments,
                      Patcher->Ranges, &Patcher->RangeCount, &Patcher->Symtab, &Patcher->HasSymtab);

  Status = InternalReadMachHeader(Collection->Buffer, Collection->BufferSize, 0, &Root);
  if (EFI_ERROR(Status))
    goto Error;

  if (!Patcher->HasSymtab)
  {
    Count = 0;
    ReadImageCommands(Collection->Buffer, 0, &Root, NULL, NULL, &Count, &Patcher->Symtab,
                      &Patcher->HasSymtab);
  }

  Total = 1;
  for (Index = 0; Index < Collection->EntryCount; ++Index)
  {
    if (Index == EntryIndex)
      continue;

    Status = InternalReadMachHeader(Collection->Buffer, Collection->BufferSize,
                                    Collection->Entries[Index].FileOffset, &Header);
    if (EFI_ERROR(Status))
      goto Error;

    Count = 0;
    ReadImageCommands(Collection->Buffer, Collection->Entries[Index].FileOffset, &Header, NULL,
                      NULL, &Count, NULL, NULL);
    if (Count > MAX_UINTN / sizeof(*Patcher->Excluded) - Total)
    {
      Status = EFI_OUT_OF_RESOURCES;
      goto Error;
    }

    Total += Count;
  }

  Patcher->Excluded = AllocateZeroPool(Total * sizeof(*Patcher->Excluded));
  if (Patcher->Excluded == NULL)
  {
    Status = EFI_OUT_OF_RESOURCES;
    goto Error;
  }

  Patcher->Excluded[0].Offset = 0;
  Patcher->Excluded[0].Size = sizeof(Root) + Root.CommandsSize;
  Patcher->ExcludedCount = 1;
  for (Index = 0; Index < Collection->EntryCount; ++Index)
  {
    if (Index == EntryIndex)
      continue;

    Status = InternalReadMachHeader(Collection->Buffer, Collection->BufferSize,
                                    Collection->Entries[Index].FileOffset, &Header);
    if (EFI_ERROR(Status))
      goto Error;

    ReadImageCommands(Collection->Buffer, Collection->Entries[Index].FileOffset, &Header, NULL,
                      Patcher->Excluded, &Patcher->ExcludedCount, NULL, NULL);
  }

  SortAndMergeRanges(Patcher->Ranges, &Patcher->RangeCount);
  SortAndMergeRanges(Patcher->Excluded, &Patcher->ExcludedCount);
  return EFI_SUCCESS;

Error:
  InternalFreePatcher(Patcher);
  return Status;
}

VOID
InternalFreePatcher(IN OUT AC_PATCHER_CONTEXT *Patcher)
{
  if (Patcher->Segments != NULL)
    FreePool(Patcher->Segments);
  if (Patcher->Ranges != NULL)
    FreePool(Patcher->Ranges);
  if (Patcher->Excluded != NULL)
    FreePool(Patcher->Excluded);

  ZeroMem(Patcher, sizeof(*Patcher));
}

EFI_STATUS
InternalPatcherAddressToOffset(IN CONST AC_PATCHER_CONTEXT *Patcher, IN UINT64 Address,
                               OUT UINTN *Offset)
{
  UINT32                 Index;
  CONST AC_MACH_SEGMENT *Segment;
  UINT64                 Delta;
  UINTN                  Value;
  BOOLEAN                Found;

  Found = FALSE;
  Value = 0;
  for (Index = 0; Index < Patcher->SegmentCount; ++Index)
  {
    Segment = &Patcher->Segments[Index];
    if (Address < Segment->VirtualAddress)
      continue;

    Delta = Address - Segment->VirtualAddress;
    if ((Delta >= Segment->VirtualSize) || (Delta >= Segment->FileSize))
      continue;

    if (Found && (Value != Segment->FileOffset + Delta))
      return EFI_COMPROMISED_DATA;

    Value = (UINTN)(Segment->FileOffset + Delta);
    Found = TRUE;
  }

  if (!Found)
    return EFI_NOT_FOUND;

  *Offset = Value;
  return EFI_SUCCESS;
}
