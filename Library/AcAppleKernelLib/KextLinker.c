/** @file
  ARM64e kext placement, symbol binding and split-segment relocation.
**/
#include "AppleKernelInternal.h"

EFI_STATUS
InternalKextMap(AC_KEXT_IMAGE *Image, UINT64 Old, UINTN Length, UINTN *Offset, UINT64 *Address)
{
  UINTN            Index;
  AC_KEXT_SEGMENT *Segment;
  for (Index = 0; Index < Image->SegmentCount; ++Index)
  {
    Segment = &Image->Segments[Index];
    if (Old < Segment->Mach.VirtualAddress ||
        !KextRange(Segment->Size, Old - Segment->Mach.VirtualAddress, Length))
      continue;
    *Offset = Segment->Offset + (UINTN)(Old - Segment->Mach.VirtualAddress);
    *Address = Segment->Address + Old - Segment->Mach.VirtualAddress;
    return EFI_SUCCESS;
  }
  return EFI_COMPROMISED_DATA;
}

STATIC EFI_STATUS
InputFile(AC_KEXT_IMAGE *Image, UINT64 Address, UINTN Size, UINTN *Offset)
{
  UINTN            Index;
  AC_MACH_SEGMENT *Segment;
  for (Index = 0; Index < Image->SegmentCount; ++Index)
  {
    Segment = &Image->Segments[Index].Mach;
    if (Address >= Segment->VirtualAddress &&
        KextRange(Segment->FileSize, Address - Segment->VirtualAddress, Size))
    {
      *Offset = (UINTN)(Segment->FileOffset + Address - Segment->VirtualAddress);
      return EFI_SUCCESS;
    }
  }
  return EFI_COMPROMISED_DATA;
}

STATIC CONST CHAR8 *
SymbolName(CONST UINT8 *Buffer, AC_MACH_SYMTAB *Table, AC_MACH_SYMBOL *Symbol)
{
  UINTN Index;
  if (Symbol->StringIndex >= Table->StringsSize)
    return NULL;
  for (Index = Symbol->StringIndex; Index < Table->StringsSize; ++Index)
    if (Buffer[Table->StringsOffset + Index] == 0)
      return (CONST CHAR8 *)Buffer + Table->StringsOffset + Symbol->StringIndex;
  return NULL;
}

EFI_STATUS
InternalKextParse(AC_KEXT_IMAGE *Image)
{
  AC_MACH_COMMAND  Command;
  AC_KEXT_SEGMENT *Segment;
  AC_KEXT_SECTION *Section;
  AC_MACH_SYMBOL   Symbol;
  CONST CHAR8     *Name;
  UINTN            At;
  UINTN            Index;
  UINTN            Other;
  UINTN            Extent;
  UINTN            HeaderEnd;
  EFI_STATUS       Status;
  Status = InternalReadMachHeader(Image->Input, Image->InputSize, 0, &Image->Header);
  if (EFI_ERROR(Status))
    return Status;
  if (Image->Header.FileType != AC_MACH_KEXT_BUNDLE || (Image->Header.CpuSubtype & 0xFFFFFFU) != 2)
    return EFI_UNSUPPORTED;
  HeaderEnd = 32 + Image->Header.CommandsSize;
  Image->SectionCount = 1;
  Image->Sections[0].Size = HeaderEnd;
  for (At = 32; At < HeaderEnd; At += Command.Size)
  {
    CopyMem(&Command, Image->Input + At, 8);
    if (Command.Type == AC_MACH_LC_SEGMENT_64)
    {
      if (Image->SegmentCount == AC_KEXT_SEGMENTS)
        return EFI_BAD_BUFFER_SIZE;
      Segment = &Image->Segments[Image->SegmentCount++];
      CopyMem(&Segment->Mach, Image->Input + At, 72);
      Segment->Command = At;
      if (Command.Size != 72 + Segment->Mach.SectionCount * 80 ||
          Segment->Mach.FileSize > Segment->Mach.VirtualSize ||
          Image->SectionCount + Segment->Mach.SectionCount > AC_KEXT_SECTIONS)
        return EFI_COMPROMISED_DATA;
      for (Other = 0; Other + 1 < Image->SegmentCount; ++Other)
      {
        AC_MACH_SEGMENT *Previous = &Image->Segments[Other].Mach;
        if (Segment->Mach.VirtualSize != 0 && Previous->VirtualSize != 0 &&
            Segment->Mach.VirtualAddress < Previous->VirtualAddress + Previous->VirtualSize &&
            Previous->VirtualAddress < Segment->Mach.VirtualAddress + Segment->Mach.VirtualSize)
          return EFI_COMPROMISED_DATA;
      }
      Extent = 0;
      if (KextName(Segment->Mach.Name, "__TEXT"))
      {
        if (Segment->Mach.VirtualAddress != 0 || Segment->Mach.FileOffset != 0)
          return EFI_UNSUPPORTED;
        Extent = HeaderEnd;
        Image->HeaderOffset = MAX_UINTN;
      }
      if (KextName(Segment->Mach.Name, "__LINKEDIT"))
        continue;
      for (Index = 0; Index < Segment->Mach.SectionCount; ++Index)
      {
        Section = &Image->Sections[Image->SectionCount++];
        CopyMem(Section, Image->Input + At + 72 + Index * 80, 80);
        if (Section->RelocationCount != 0 || Section->Alignment > 14)
          return EFI_UNSUPPORTED;
        if (Section->Address < Segment->Mach.VirtualAddress ||
            !KextRange(Segment->Mach.VirtualSize, Section->Address - Segment->Mach.VirtualAddress,
                       Section->Size))
          return EFI_COMPROMISED_DATA;
        if ((Section->Flags & 0xFFU) != 1 && (Section->Flags & 0xFFU) != 0xCU &&
            (Section->Flags & 0xFFU) != 0x12U && Section->Size != 0 &&
            (Section->Offset !=
               Segment->Mach.FileOffset + Section->Address - Segment->Mach.VirtualAddress ||
             !KextRange(Image->InputSize, Section->Offset, Section->Size)))
          return EFI_COMPROMISED_DATA;
        Extent =
          MAX(Extent, (UINTN)(Section->Address - Segment->Mach.VirtualAddress + Section->Size));
      }
      Segment->Size = Extent == 0 ? (UINTN)Segment->Mach.FileSize : Extent;
      if (Segment->Size > AC_KEXT_LIMIT)
        return EFI_BAD_BUFFER_SIZE;
    }
    else if (Command.Type == AC_MACH_LC_SYMTAB)
      CopyMem(&Image->Symbols, Image->Input + At, 24);
    else if (Command.Type == AC_MACH_LC_DYLD_CHAINED_FIXUPS || Command.Type == 0x1EU)
    {
      AC_MACH_LINKEDIT_DATA Data;
      if (Command.Size != 16)
        return EFI_COMPROMISED_DATA;
      CopyMem(&Data, Image->Input + At, 16);
      if (!KextRange(Image->InputSize, Data.DataOffset, Data.DataSize))
        return EFI_COMPROMISED_DATA;
      if (Command.Type == 0x1EU)
      {
        if (Image->Split.Command.Type != 0)
          return EFI_COMPROMISED_DATA;
        Image->Split = Data;
      }
      else
      {
        if (Image->Chains.Command.Type != 0)
          return EFI_COMPROMISED_DATA;
        Image->Chains = Data;
      }
    }
    else if (Command.Type != 0xBU && Command.Type != 0x1BU && Command.Type != 0x1DU &&
             Command.Type != 0x26U && Command.Type != 0x29U && Command.Type != 0x2AU &&
             Command.Type != 0x32U && Command.Type != 0x80000033U)
      return EFI_UNSUPPORTED;
  }
  if (Image->HeaderOffset != MAX_UINTN || Image->Symbols.Command.Type == 0 ||
      Image->Chains.DataSize < 28 || Image->Split.DataSize < 2)
    return EFI_UNSUPPORTED;
  for (Index = 0; Index < Image->Symbols.SymbolCount; ++Index)
  {
    CopyMem(&Symbol, Image->Input + Image->Symbols.SymbolsOffset + Index * 16, 16);
    Name = SymbolName(Image->Input, &Image->Symbols, &Symbol);
    if (Name == NULL)
      return EFI_COMPROMISED_DATA;
    if ((Symbol.Type & 0xE0U) != 0)
      continue;
    if ((Symbol.Type & 0xEU) != 0 && (Symbol.Type & 0xEU) != 0xEU && (Symbol.Type & 0xEU) != 2)
      return EFI_UNSUPPORTED;
    if ((Symbol.Type & 0xEU) == 0xEU &&
        (Symbol.Section == 0 || Symbol.Section >= Image->SectionCount))
      return EFI_COMPROMISED_DATA;
    if ((Symbol.Type & 0xEU) != 0)
    {
      ++Image->SymbolCount;
      Image->StringSize += AsciiStrLen(Name) + 1;
      if (AsciiStrCmp(Name, "_kmod_info") == 0 && (Symbol.Type & 0xEU) == 0xEU)
        Image->Kmod = Symbol.Value;
    }
  }
  ++Image->StringSize;
  if (Image->Kmod == 0 || Image->SymbolCount == 0)
    return EFI_NOT_FOUND;
  return EFI_SUCCESS;
}

EFI_STATUS
InternalKextPlace(AC_KEXT_CONTEXT *Context, AC_KEXT_IMAGE *Image)
{
  UINTN            Index;
  UINTN            Other;
  UINTN            Alignment;
  UINTN            Size;
  CONST CHAR8     *RootName;
  AC_KEXT_SEGMENT *Segment;
  EFI_STATUS       Status;
  BOOLEAN          HasLinkedit = FALSE;
  for (Index = 0; Index < Image->SegmentCount; ++Index)
  {
    Segment = &Image->Segments[Index];
    if (KextName(Segment->Mach.Name, "__LINKEDIT"))
    {
      if (HasLinkedit)
        return EFI_COMPROMISED_DATA;
      HasLinkedit = TRUE;
      Segment->Size = Image->SymbolCount * 16 + Image->StringSize;
      RootName = "__LINKEDIT";
    }
    else if (KextName(Segment->Mach.Name, "__TEXT_EXEC"))
      RootName = "__TEXT_EXEC";
    else if (KextName(Segment->Mach.Name, "__TEXT") || KextName(Segment->Mach.Name, "__DATA_CONST"))
    {
      RootName = "__TEXT";
      for (Other = 0; Other < Context->RootCount; ++Other)
        if (KextName(Context->Roots[Other].Mach.Name, "__DATA_CONST"))
          RootName = "__DATA_CONST";
      for (Other = 0; Other < Context->RootCount; ++Other)
        if (KextName(Context->Roots[Other].Mach.Name, "__PRELINK_TEXT"))
          RootName = "__PRELINK_TEXT";
    }
    else if (KextName(Segment->Mach.Name, "__DATA"))
      RootName = "__DATA";
    else
      return EFI_UNSUPPORTED;
    if (Segment->Size == 0)
      return EFI_UNSUPPORTED;
    Alignment = 16;
    for (Other = 1; Other < Image->SectionCount; ++Other)
      if (Image->Sections[Other].Address >= Segment->Mach.VirtualAddress &&
          Image->Sections[Other].Address < Segment->Mach.VirtualAddress + Segment->Mach.VirtualSize)
        Alignment = MAX(Alignment, (UINTN)1 << Image->Sections[Other].Alignment);
    Status = InternalKextAllocate(Context, RootName, Segment->Size, Alignment, &Segment->Offset,
                                  &Segment->Address);
    if (Status == EFI_OUT_OF_RESOURCES &&
        (KextName(Segment->Mach.Name, "__TEXT") || KextName(Segment->Mach.Name, "__DATA_CONST")))
    {
      STATIC CONST CHAR8 *Alternatives[] = {"__PRELINK_TEXT", "__DATA_CONST", "__TEXT"};
      UINTN               Candidate;
      for (Candidate = 0; Candidate < ARRAY_SIZE(Alternatives); ++Candidate)
      {
        if (AsciiStrCmp(Alternatives[Candidate], RootName) == 0)
          continue;
        for (Other = 0; Other < Context->RootCount; ++Other)
          if (KextName(Context->Roots[Other].Mach.Name, Alternatives[Candidate]))
            break;
        if (Other == Context->RootCount)
          continue;
        Status = InternalKextAllocate(Context, Alternatives[Candidate], Segment->Size, Alignment,
                                      &Segment->Offset, &Segment->Address);
        if (Status != EFI_OUT_OF_RESOURCES)
          break;
      }
    }
    if (EFI_ERROR(Status))
      return Status;
    if (KextName(Segment->Mach.Name, "__TEXT"))
    {
      Image->HeaderOffset = Segment->Offset;
      Image->HeaderAddress = Segment->Address;
      Image->TextSize = Segment->Size;
      Image->Sections[0].Address = 0;
    }
    if (KextName(Segment->Mach.Name, "__LINKEDIT"))
    {
      Image->SymbolOffset = Segment->Offset;
      Image->StringOffset = Segment->Offset + Image->SymbolCount * 16;
    }
    else
    {
      Size = MIN((UINTN)Segment->Mach.FileSize, Segment->Size);
      CopyMem(Context->Buffer + Segment->Offset, Image->Input + Segment->Mach.FileOffset, Size);
    }
  }
  return HasLinkedit ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

STATIC EFI_STATUS
LookupSymbol(CONST UINT8 *Buffer, UINTN Size, UINTN HeaderOffset, CONST CHAR8 *Name,
             UINT64 *Address)
{
  AC_MACH_HEADER  Header;
  AC_MACH_COMMAND Command;
  AC_MACH_SYMTAB  Table;
  AC_MACH_SYMBOL  Symbol;
  CONST CHAR8    *Current;
  UINTN           At;
  UINTN           Index;
  EFI_STATUS      Status = InternalReadMachHeader(Buffer, Size, HeaderOffset, &Header);
  if (EFI_ERROR(Status))
    return Status;
  for (At = HeaderOffset + 32; At < HeaderOffset + 32 + Header.CommandsSize; At += Command.Size)
  {
    CopyMem(&Command, Buffer + At, 8);
    if (Command.Type != AC_MACH_LC_SYMTAB)
      continue;
    CopyMem(&Table, Buffer + At, 24);
    for (Index = 0; Index < Table.SymbolCount; ++Index)
    {
      CopyMem(&Symbol, Buffer + Table.SymbolsOffset + Index * 16, 16);
      if ((Symbol.Type & 0xE0U) != 0 || (Symbol.Type & 1U) == 0 ||
          ((Symbol.Type & 0xEU) != 0xEU && (Symbol.Type & 0xEU) != 2))
        continue;
      Current = SymbolName(Buffer, &Table, &Symbol);
      if (Current == NULL)
        return EFI_COMPROMISED_DATA;
      if (AsciiStrCmp(Current, Name) == 0)
      {
        *Address = Symbol.Value;
        return EFI_SUCCESS;
      }
    }
  }
  return EFI_NOT_FOUND;
}

EFI_STATUS
InternalKextResolve(AC_KEXT_CONTEXT *Context, UINTN Index, CONST CHAR8 *Name, UINT64 *Address)
{
  UINTN          Provider;
  EFI_STATUS     Status;
  AC_KEXT_IMAGE *Image = &Context->Images[Index];
  // All providers are already validated. Restrict external lookup to declared dependencies.
  for (Provider = 0; Provider < Context->Source->EntryCount; ++Provider)
  {
    CONST CHAR8 *Identifier = Context->Source->Entries[Provider].Identifier;
    BOOLEAN      Allowed = InternalKextProperty(Image->Libraries, Identifier) != NULL;
    if (Provider == Context->Source->KernelIndex)
    {
      UINTN                Field;
      CONST CHAR8         *Key;
      CONST AC_PLIST_NODE *Value;
      for (Field = 0; Field < AcPlistGetCount(Image->Libraries); ++Field)
      {
        AcPlistGetDictionaryItem(Image->Libraries, Field, &Key, &Value);
        if (AsciiStrnCmp(Key, "com.apple.kpi.", 14) == 0 ||
            AsciiStrCmp(Key, "com.apple.kernel") == 0)
          Allowed = TRUE;
      }
    }
    if (!Allowed)
      continue;
    Status = LookupSymbol(Context->Source->Buffer, Context->Source->BufferSize,
                          Context->Source->Entries[Provider].FileOffset, Name, Address);
    if (Status != EFI_NOT_FOUND)
      return Status;
  }
  for (Provider = 0; Provider < Index; ++Provider)
  {
    AC_KEXT_IMAGE *Dependency = &Context->Images[Provider];
    if (Dependency->Input == NULL ||
        InternalKextProperty(Image->Libraries, Dependency->Identifier) == NULL)
      continue;
    Status = LookupSymbol(Context->Buffer, Context->Size, Dependency->HeaderOffset, Name, Address);
    if (Status != EFI_NOT_FOUND)
      return Status;
  }
  return EFI_NOT_FOUND;
}

STATIC EFI_STATUS
LinkChains(AC_KEXT_CONTEXT *Context, AC_KEXT_IMAGE *Image, UINTN Index)
{
  CONST UINT8 *Blob = Image->Input + Image->Chains.DataOffset;
  CONST UINT8 *Starts;
  CONST UINT8 *Record;
  UINTN        Size = Image->Chains.DataSize;
  UINTN        StartOffset = KextRead32(Blob + 4);
  UINTN        Imports = KextRead32(Blob + 8);
  UINTN        Names = KextRead32(Blob + 12);
  UINTN        ImportCount = KextRead32(Blob + 16);
  UINTN        ImportFormat = KextRead32(Blob + 20);
  UINTN        ImportStride;
  UINTN        SegmentIndex;
  UINTN        Relative;
  UINTN        RecordSize;
  UINTN        Pages;
  UINTN        PageSize;
  UINTN        Page;
  UINTN        Start;
  UINTN        Site;
  UINTN        File;
  UINTN        Next;
  UINTN        Ordinal;
  UINTN        NameOffset;
  UINTN        End;
  UINTN        Output;
  UINT64       Address;
  UINT64       Target;
  UINT64       Word;
  UINT64       Body;
  INT64        Addend;
  EFI_STATUS   Status;
  if (KextRead32(Blob) != 0 || KextRead32(Blob + 24) != 0)
    return EFI_UNSUPPORTED;
  ImportStride = ImportFormat == 1 ? 4 : ImportFormat == 2 ? 8 : ImportFormat == 3 ? 16 : 0;
  if (ImportStride == 0)
    return EFI_UNSUPPORTED;
  if (StartOffset < 28 || Names > Size || Imports > Size ||
      ImportCount > (Size - Imports) / ImportStride ||
      !KextRange(Size, StartOffset, 4 + Image->SegmentCount * 4))
    return EFI_COMPROMISED_DATA;
  Starts = Blob + StartOffset;
  if (KextRead32(Starts) != Image->SegmentCount)
    return EFI_COMPROMISED_DATA;
  for (SegmentIndex = 0; SegmentIndex < Image->SegmentCount; ++SegmentIndex)
  {
    Relative = KextRead32(Starts + 4 + SegmentIndex * 4);
    if (Relative == 0)
      continue;
    if (!KextRange(Size - StartOffset, Relative, 22))
      return EFI_COMPROMISED_DATA;
    Record = Starts + Relative;
    RecordSize = KextRead32(Record);
    PageSize = KextRead16(Record + 4);
    Pages = KextRead16(Record + 20);
    if (RecordSize < 22 + Pages * 2 || !KextRange(Size - StartOffset, Relative, RecordSize))
      return EFI_COMPROMISED_DATA;
    if (KextRead16(Record + 6) != 7 || (PageSize != 0x1000 && PageSize != AC_KEXT_PAGE) ||
        KextRead64(Record + 8) != Image->Segments[SegmentIndex].Mach.VirtualAddress)
      return EFI_UNSUPPORTED;
    for (Page = 0; Page < Pages; ++Page)
    {
      Start = KextRead16(Record + 22 + Page * 2);
      if (Start == 0xFFFFU)
        continue;
      if ((Start & 0x8000U) != 0)
        return EFI_UNSUPPORTED;
      Site = Image->Segments[SegmentIndex].Mach.VirtualAddress + Page * PageSize + Start;
      for (;;)
      {
        if (Site < Image->Segments[SegmentIndex].Mach.VirtualAddress + Page * PageSize ||
            Site >= Image->Segments[SegmentIndex].Mach.VirtualAddress + (Page + 1) * PageSize)
          return EFI_COMPROMISED_DATA;
        Status = InputFile(Image, Site, 8, &File);
        if (EFI_ERROR(Status))
          return Status;
        Word = KextRead64(Image->Input + File);
        Next = (UINTN)((Word >> 51) & 0x7FFU);
        if ((Word & (1ULL << 62)) != 0)
        {
          UINT64 Import;
          UINTN  Weak;
          Ordinal = (UINTN)(Word & 0xFFFFU);
          if (Ordinal >= ImportCount || (Word & 0xFFFF0000ULL) != 0)
            return EFI_COMPROMISED_DATA;
          Import = ImportFormat == 3 ? KextRead64(Blob + Imports + Ordinal * ImportStride)
                                     : KextRead32(Blob + Imports + Ordinal * ImportStride);
          NameOffset = ImportFormat == 3 ? (UINTN)(Import >> 32) : (UINTN)(Import >> 9);
          Weak = (UINTN)((Import >> (ImportFormat == 3 ? 16 : 8)) & 1);
          if (!KextRange(Size - Names, NameOffset, 1))
            return EFI_COMPROMISED_DATA;
          for (End = Names + NameOffset; End < Size && Blob[End] != 0; ++End)
          {
          }
          if (End == Size)
            return EFI_COMPROMISED_DATA;
          Status =
            InternalKextResolve(Context, Index, (CONST CHAR8 *)Blob + Names + NameOffset, &Target);
          // Weak nulls have no cache-relative representation; reject instead of binding to KC base.
          if (Status == EFI_NOT_FOUND && Weak != 0)
            return EFI_UNSUPPORTED;
          if (EFI_ERROR(Status))
            return Status;
          Addend = (Word >> 63) != 0 ? 0 : (INT64)((Word >> 32) & 0x7FFFFU);
          if ((Addend & 0x40000) != 0)
            Addend -= 0x80000;
          if (ImportFormat == 2)
            Addend += (INT32)KextRead32(Blob + Imports + Ordinal * ImportStride + 4);
          if (ImportFormat == 3)
          {
            INT64 Extra = (INT64)KextRead64(Blob + Imports + Ordinal * ImportStride + 8);
            if ((Extra > 0 && Addend > MAX_INT64 - Extra) ||
                (Extra < 0 && Addend < MIN_INT64 - Extra))
              return EFI_BAD_BUFFER_SIZE;
            Addend += Extra;
          }
          if ((Addend >= 0 && Target > MAX_UINT64 - (UINT64)Addend) ||
              (Addend < 0 && Target < (UINT64)(-(Addend + 1)) + 1))
            return EFI_BAD_BUFFER_SIZE;
          Target += (UINT64)Addend;
        }
        else
        {
          if ((Word >> 63) == 0 && ((Word >> 43) & 0xFFU) != 0)
            return EFI_UNSUPPORTED;
          Target = Word & ((Word >> 63) != 0 ? 0xFFFFFFFFULL : 0x7FFFFFFFFFFULL);
          Status = InternalKextMap(Image, Target, 1, &Output, &Address);
          if (EFI_ERROR(Status))
            return Status;
          Target = Address;
        }
        if (Target < Context->Base || Target - Context->Base >= 0x40000000ULL)
          return EFI_UNSUPPORTED;
        Body = Target - Context->Base;
        if ((Word >> 63) != 0)
          Body |= Word & 0x8007FFFF00000000ULL;
        Status = InternalKextMap(Image, Site, 8, &Output, &Address);
        if (!EFI_ERROR(Status))
          Status = InternalKextAddFixup(Context, Output, Body);
        if (EFI_ERROR(Status))
          return Status;
        if (Next == 0)
          break;
        Site += Next * 4;
      }
    }
  }
  return EFI_SUCCESS;
}

STATIC BOOLEAN
ReadUleb(CONST UINT8 **Cursor, CONST UINT8 *End, UINT64 *Value)
{
  UINTN Shift = 0;
  UINT8 Byte;
  *Value = 0;
  do
  {
    if (*Cursor == End || Shift > 63)
      return FALSE;
    Byte = *(*Cursor)++;
    if (Shift == 63 && (Byte & 0x7EU) != 0)
      return FALSE;
    *Value |= (UINT64)(Byte & 0x7FU) << Shift;
    Shift += 7;
  } while ((Byte & 0x80U) != 0);
  return TRUE;
}

STATIC EFI_STATUS
Relocate(AC_KEXT_CONTEXT *Context, AC_KEXT_IMAGE *Image, UINT64 Kind, UINT64 From, UINT64 To)
{
  UINTN      File;
  UINTN      Other;
  UINT64     Address;
  UINT64     Target;
  INT64      Delta;
  INT64      Pages;
  UINT32     Word;
  UINTN      Scale;
  EFI_STATUS Status;
  UINTN      Width = Kind == 2 || Kind == 4 || Kind == 13 ? 8 : 4;
  Status = InternalKextMap(Image, From, Width, &File, &Address);
  if (!EFI_ERROR(Status))
    Status = InternalKextMap(Image, To, 0, &Other, &Target);
  if (EFI_ERROR(Status))
    return Status;
  Delta = (INT64)(Target - Address);
  Word = KextRead32(Context->Buffer + File);
  if (Kind == 2 || Kind == 13)
  {
    for (Other = Image->FixupStart; Other < Context->FixupCount; ++Other)
      if (Context->Fixups[Other].Offset == File)
        return (Context->Fixups[Other].Body & 0x3FFFFFFFULL) == Target - Context->Base
                 ? EFI_SUCCESS
                 : EFI_COMPROMISED_DATA;
    return EFI_COMPROMISED_DATA;
  }
  if (Kind == 3)
  {
    if (Delta < MIN_INT32 || Delta > MAX_INT32)
      return EFI_UNSUPPORTED;
    Word = (UINT32)Delta;
  }
  else if (Kind == 4)
  {
    KextWrite64(Context->Buffer + File, (UINT64)Delta);
    return EFI_SUCCESS;
  }
  else if (Kind == 5)
  {
    if ((Word & 0x9F000000U) != 0x90000000U)
      return EFI_COMPROMISED_DATA;
    Pages = (INT64)(Target >> 12) - (INT64)(Address >> 12);
    if (Pages < -0x100000 || Pages >= 0x100000)
      return EFI_UNSUPPORTED;
    Word = (Word & 0x9F00001FU) | (((UINT32)Pages & 3U) << 29) |
           ((((UINT32)Pages >> 2) & 0x7FFFFU) << 5);
  }
  else if (Kind == 6)
  {
    if ((Word & 0x7FC00000U) == 0x11000000U)
      Scale = 0;
    else if ((Word & 0x3B000000U) == 0x39000000U)
    {
      Scale = Word >> 30;
      if ((Word & 0x04800000U) == 0x04800000U)
        Scale = 4;
    }
    else
      return EFI_UNSUPPORTED;
    if ((Target & (((UINT64)1 << Scale) - 1)) != 0)
      return EFI_COMPROMISED_DATA;
    Word = (Word & ~(0xFFFU << 10)) | ((UINT32)((Target & 0xFFFU) >> Scale) << 10);
  }
  else if (Kind == 7)
  {
    if ((Word & 0x7C000000U) != 0x14000000U || (Delta & 3) != 0 || Delta < -0x8000000 ||
        Delta >= 0x8000000)
      return EFI_UNSUPPORTED;
    Word = (Word & 0xFC000000U) | ((UINT32)(Delta / 4) & 0x3FFFFFFU);
  }
  else if (Kind == 12)
  {
    if (Target < Image->HeaderAddress || Target - Image->HeaderAddress > MAX_UINT32)
      return EFI_UNSUPPORTED;
    Word = (UINT32)(Target - Image->HeaderAddress);
  }
  else
    return EFI_UNSUPPORTED;
  KextWrite32(Context->Buffer + File, Word);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
LinkSplit(AC_KEXT_CONTEXT *Context, AC_KEXT_IMAGE *Image)
{
  CONST UINT8 *Cursor = Image->Input + Image->Split.DataOffset;
  CONST UINT8 *End = Cursor + Image->Split.DataSize;
  UINT64       Groups;
  UINT64       From;
  UINT64       To;
  UINT64       Targets;
  UINT64       TargetOffset;
  UINT64       Kinds;
  UINT64       Kind;
  UINT64       References;
  UINT64       Offset;
  UINT64       Delta;
  EFI_STATUS   Status;
  if (*Cursor++ != 0x7FU || !ReadUleb(&Cursor, End, &Groups))
    return EFI_UNSUPPORTED;
  while (Groups-- != 0)
  {
    if (!ReadUleb(&Cursor, End, &From) || !ReadUleb(&Cursor, End, &To) ||
        From >= Image->SectionCount || To >= Image->SectionCount ||
        !ReadUleb(&Cursor, End, &Targets))
      return EFI_COMPROMISED_DATA;
    TargetOffset = 0;
    while (Targets-- != 0)
    {
      if (!ReadUleb(&Cursor, End, &Delta) || Delta > MAX_UINT64 - TargetOffset ||
          !ReadUleb(&Cursor, End, &Kinds))
        return EFI_COMPROMISED_DATA;
      TargetOffset += Delta;
      if (TargetOffset > Image->Sections[To].Size)
        return EFI_COMPROMISED_DATA;
      while (Kinds-- != 0)
      {
        if (!ReadUleb(&Cursor, End, &Kind) || !ReadUleb(&Cursor, End, &References))
          return EFI_COMPROMISED_DATA;
        Offset = 0;
        while (References-- != 0)
        {
          UINTN Width = Kind == 2 || Kind == 4 || Kind == 13 ? 8 : 4;
          if (!ReadUleb(&Cursor, End, &Delta) || Delta > MAX_UINT64 - Offset)
            return EFI_COMPROMISED_DATA;
          Offset += Delta;
          if (!KextRange((UINTN)Image->Sections[From].Size, Offset, Width))
            return EFI_COMPROMISED_DATA;
          Status = Relocate(Context, Image, Kind, Image->Sections[From].Address + Offset,
                            Image->Sections[To].Address + TargetOffset);
          if (EFI_ERROR(Status))
            return Status;
        }
      }
    }
  }
  // Linkers may pad split info with zero bytes.
  while (Cursor < End)
    if (*Cursor++ != 0)
      return EFI_COMPROMISED_DATA;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
WriteSymbols(AC_KEXT_CONTEXT *Context, AC_KEXT_IMAGE *Image)
{
  AC_MACH_SYMBOL Symbol;
  CONST CHAR8   *Name;
  UINTN          Index;
  UINTN          Used = 0;
  UINTN          String = 1;
  UINTN          Output;
  UINT64         Address;
  EFI_STATUS     Status;
  for (Index = 0; Index < Image->Symbols.SymbolCount; ++Index)
  {
    CopyMem(&Symbol, Image->Input + Image->Symbols.SymbolsOffset + Index * 16, 16);
    if ((Symbol.Type & 0xE0U) != 0 || (Symbol.Type & 0xEU) == 0)
      continue;
    Name = SymbolName(Image->Input, &Image->Symbols, &Symbol);
    if (Name == NULL)
      return EFI_COMPROMISED_DATA;
    if ((Symbol.Type & 0xEU) == 0xEU)
    {
      Status = InternalKextMap(Image, Symbol.Value, 0, &Output, &Address);
      if (EFI_ERROR(Status))
        return Status;
      Symbol.Value = Address;
    }
    Symbol.StringIndex = (UINT32)String;
    CopyMem(Context->Buffer + Image->SymbolOffset + Used * 16, &Symbol, 16);
    CopyMem(Context->Buffer + Image->StringOffset + String, Name, AsciiStrLen(Name) + 1);
    String += AsciiStrLen(Name) + 1;
    ++Used;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
WriteCommands(AC_KEXT_CONTEXT *Context, AC_KEXT_IMAGE *Image)
{
  AC_MACH_COMMAND Command;
  AC_MACH_HEADER  Header = Image->Header;
  AC_MACH_SEGMENT Segment;
  AC_KEXT_SECTION Section;
  AC_MACH_SYMTAB  Symbols = Image->Symbols;
  UINTN           At;
  UINTN           Out = Image->HeaderOffset + 32;
  UINTN           SegmentIndex = 0;
  UINTN           Index;
  UINTN           Mapped;
  UINT64          Address;
  EFI_STATUS      Status;
  Header.CommandCount = 0;
  Header.CommandsSize = 0;
  Header.Flags |= AC_MACH_DYLIB_IN_CACHE;
  for (At = 32; At < 32 + Image->Header.CommandsSize; At += Command.Size)
  {
    CopyMem(&Command, Image->Input + At, 8);
    // Relocation metadata and signatures no longer describe the linked image.
    if (Command.Type != AC_MACH_LC_SEGMENT_64 && Command.Type != AC_MACH_LC_SYMTAB &&
        Command.Type != 0x1BU && Command.Type != 0x2AU && Command.Type != 0x32U)
      continue;
    if (Command.Type == AC_MACH_LC_SEGMENT_64)
    {
      AC_KEXT_SEGMENT *Placed = &Image->Segments[SegmentIndex++];
      Segment = Placed->Mach;
      Segment.VirtualAddress = Placed->Address;
      Segment.VirtualSize = Placed->Size;
      Segment.FileOffset = Placed->Offset;
      Segment.FileSize = Placed->Size;
      CopyMem(Context->Buffer + Out, &Segment, 72);
      for (Index = 0; Index < Segment.SectionCount; ++Index)
      {
        CopyMem(&Section, Image->Input + At + 72 + Index * 80, 80);
        Status = InternalKextMap(Image, Section.Address, (UINTN)Section.Size, &Mapped, &Address);
        if (EFI_ERROR(Status))
          return Status;
        Section.Address = Address;
        if ((Section.Flags & 0xFFU) != 1 && (Section.Flags & 0xFFU) != 0xCU &&
            (Section.Flags & 0xFFU) != 0x12U)
          Section.Offset = (UINT32)Mapped;
        else
          Section.Offset = 0;
        Section.Relocations = Section.RelocationCount = 0;
        CopyMem(Context->Buffer + Out + 72 + Index * 80, &Section, 80);
      }
    }
    else if (Command.Type == AC_MACH_LC_SYMTAB)
    {
      Symbols.SymbolsOffset = (UINT32)Image->SymbolOffset;
      Symbols.SymbolCount = (UINT32)Image->SymbolCount;
      Symbols.StringsOffset = (UINT32)Image->StringOffset;
      Symbols.StringsSize = (UINT32)Image->StringSize;
      CopyMem(Context->Buffer + Out, &Symbols, 24);
    }
    else
      CopyMem(Context->Buffer + Out, Image->Input + At, Command.Size);
    Out += Command.Size;
    ++Header.CommandCount;
    Header.CommandsSize += Command.Size;
  }
  CopyMem(Context->Buffer + Image->HeaderOffset, &Header, 32);
  return EFI_SUCCESS;
}

EFI_STATUS
InternalKextLink(AC_KEXT_CONTEXT *Context, AC_KEXT_IMAGE *Image, UINTN Index)
{
  UINTN      Offset;
  UINT64     Address;
  EFI_STATUS Status;
  Image->FixupStart = Context->FixupCount;
  Status = LinkChains(Context, Image, Index);
  if (!EFI_ERROR(Status))
    Status = LinkSplit(Context, Image);
  if (!EFI_ERROR(Status))
    Status = WriteSymbols(Context, Image);
  if (!EFI_ERROR(Status))
    Status = WriteCommands(Context, Image);
  if (!EFI_ERROR(Status))
    Status = InternalKextMap(Image, Image->Kmod, 196, &Offset, &Address);
  if (EFI_ERROR(Status))
    return Status;
  Image->Kmod = Address;
  // These fields are adjusted by OSKext, rather than dyld pointer fixups.
  KextWrite64(Context->Buffer + Offset + 156, 0);
  KextWrite64(Context->Buffer + Offset + 164, Image->TextSize);
  KextWrite64(Context->Buffer + Offset + 172,
              32 + KextRead32(Context->Buffer + Image->HeaderOffset + 20));
  return EFI_SUCCESS;
}
