/** @file
  Boot kernel collection layout validation and segment loading.
**/

#include "AppleKernelInternal.h"

#define AC_KERNEL_PAGE_SIZE 0x4000U
#define AC_KERNEL_BLOCK_SIZE 0x2000000U
#define AC_KERNEL_MAX_SEGMENTS 256U
#define AC_KERNEL_MAX_LOAD_COMMANDS 65536U

typedef struct
{
  AC_MACH_SEGMENT    *Segments;
  UINT32              SegmentCount;
  AC_KERNEL_LOAD_INFO Info;
} AC_KERNEL_LAYOUT;

STATIC EFI_STATUS
CheckSegment(IN CONST AC_MACH_SEGMENT *Segment)
{
  if ((Segment->FileSize > Segment->VirtualSize) ||
      ((Segment->InitialProtection & ~Segment->MaximumProtection) != 0))
    return EFI_COMPROMISED_DATA;
  if (((Segment->MaximumProtection | Segment->InitialProtection) & ~7U) != 0 ||
      (Segment->Flags & ~(AC_MACH_SG_NORELOC | AC_MACH_SG_READ_ONLY)) != 0)
    return EFI_UNSUPPORTED;
  return EFI_SUCCESS;
}

STATIC BOOLEAN
FileRangeIsMapped(IN CONST AC_KERNEL_LAYOUT *Layout, IN UINT64 Offset, IN UINT64 Size,
                  IN UINT64 Address)
{
  UINT32 Index;

  for (Index = 0; Index < Layout->SegmentCount; ++Index)
  {
    CONST AC_MACH_SEGMENT *Segment = &Layout->Segments[Index];
    if ((Offset >= Segment->FileOffset) && (Offset - Segment->FileOffset <= Segment->FileSize) &&
        (Size <= Segment->FileSize - (Offset - Segment->FileOffset)) &&
        (Address == Segment->VirtualAddress + (Offset - Segment->FileOffset)))
      return TRUE;
  }
  return FALSE;
}

STATIC BOOLEAN
LinkeditRangeIsMapped(IN CONST AC_KERNEL_LAYOUT *Layout, IN UINT64 Offset, IN UINT64 Size)
{
  STATIC CONST CHAR8 Name[16] = "__LINKEDIT";
  UINT32             Index;

  if (Size == 0)
    return TRUE;
  for (Index = 0; Index < Layout->SegmentCount; ++Index)
  {
    CONST AC_MACH_SEGMENT *Segment = &Layout->Segments[Index];
    if ((CompareMem(Segment->Name, Name, sizeof(Name)) == 0) && (Offset >= Segment->FileOffset) &&
        (Offset - Segment->FileOffset <= Segment->FileSize) &&
        (Size <= Segment->FileSize - (Offset - Segment->FileOffset)))
      return TRUE;
  }
  return FALSE;
}

STATIC EFI_STATUS
CheckLinkedit(IN CONST UINT8 *Buffer, IN UINTN Offset, IN CONST AC_MACH_COMMAND *Command,
              IN CONST AC_KERNEL_LAYOUT *Layout)
{
  AC_MACH_SYMTAB        Symtab;
  AC_MACH_LINKEDIT_DATA Fixups;

  if (Command->Type == AC_MACH_LC_SYMTAB)
  {
    CopyMem(&Symtab, Buffer + Offset, sizeof(Symtab));
    if (!LinkeditRangeIsMapped(Layout, Symtab.SymbolsOffset,
                               (UINT64)Symtab.SymbolCount * sizeof(AC_MACH_SYMBOL)) ||
        !LinkeditRangeIsMapped(Layout, Symtab.StringsOffset, Symtab.StringsSize))
      return EFI_COMPROMISED_DATA;
  }
  else if (Command->Type == AC_MACH_LC_DYLD_CHAINED_FIXUPS)
  {
    if (Command->Size != sizeof(Fixups))
      return EFI_COMPROMISED_DATA;
    CopyMem(&Fixups, Buffer + Offset, sizeof(Fixups));
    if ((Fixups.DataSize == 0) ||
        !LinkeditRangeIsMapped(Layout, Fixups.DataOffset, Fixups.DataSize))
      return EFI_COMPROMISED_DATA;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
CheckEmbeddedSegment(IN CONST UINT8 *Buffer, IN CONST AC_MACH_SEGMENT *Child,
                     IN CONST AC_KERNEL_LAYOUT *Layout, IN OUT UINTN *ZeroFillBudget)
{
  UINT32     Index;
  UINT64     Delta;
  UINT64     FileTail;
  UINT64     Byte;
  EFI_STATUS Status;

  Status = CheckSegment(Child);
  if (EFI_ERROR(Status) || Child->VirtualSize == 0)
    return Status;
  for (Index = 0; Index < Layout->SegmentCount; ++Index)
  {
    CONST AC_MACH_SEGMENT *Parent = &Layout->Segments[Index];
    if (Child->VirtualAddress < Parent->VirtualAddress)
      continue;
    Delta = Child->VirtualAddress - Parent->VirtualAddress;
    if ((Delta > Parent->VirtualSize) || (Child->VirtualSize > Parent->VirtualSize - Delta))
      continue;
    if (Child->FileSize != 0 &&
        !FileRangeIsMapped(Layout, Child->FileOffset, Child->FileSize, Child->VirtualAddress))
      return EFI_COMPROMISED_DATA;
    // An outer file-backed span may include padding for an embedded zero-fill tail.
    FileTail = MIN(Parent->FileSize, Delta + Child->VirtualSize);
    if (FileTail > Delta + Child->FileSize)
    {
      UINT64 Bytes = FileTail - (Delta + Child->FileSize);
      if (Bytes > *ZeroFillBudget)
        return EFI_BAD_BUFFER_SIZE;
      *ZeroFillBudget -= (UINTN)Bytes;
    }
    for (Byte = Delta + Child->FileSize; Byte < FileTail; ++Byte)
      if (Buffer[Parent->FileOffset + Byte] != 0)
        return EFI_COMPROMISED_DATA;
    return EFI_SUCCESS;
  }
  return EFI_UNSUPPORTED;
}

STATIC EFI_STATUS
ReadLayout(IN CONST AC_KERNEL_COLLECTION_CONTEXT *Context, IN UINTN MaxMemorySize,
           OUT AC_KERNEL_LAYOUT *Layout)
{
  STATIC CONST CHAR8 TextName[16] = "__TEXT";
  STATIC CONST CHAR8 LinkeditName[16] = "__LINKEDIT";
  AC_MACH_HEADER     Header;
  AC_MACH_HEADER     ChildHeader;
  AC_MACH_COMMAND    Command;
  AC_MACH_SEGMENT    Segment;
  AC_MACH_SEGMENT    TextSegment;
  AC_MACH_UNIXTHREAD Thread;
  EFI_STATUS         Status;
  UINTN              Offset;
  UINTN              End;
  UINTN              ChildOffset;
  UINTN              ChildEnd;
  UINTN              ZeroFillBudget;
  UINT64             Low = MAX_UINT64;
  UINT64             High = 0;
  UINT64             Span;
  UINT32             Index;
  UINT32             Other;
  UINT32             Commands;
  BOOLEAN            HasEntry = FALSE;
  BOOLEAN            HasText = FALSE;
  BOOLEAN            HasLinkedit = FALSE;
  BOOLEAN            HasFixups = FALSE;
  BOOLEAN            EntryInKernel = FALSE;

  ZeroMem(Layout, sizeof(*Layout));
  if ((Context == NULL) || (Context->Buffer == NULL) || (Context->Entries == NULL) ||
      (Context->EntryCount == 0) || (Context->KernelIndex >= Context->EntryCount) ||
      (Context->BufferSize > Context->BufferCapacity) || (MaxMemorySize == 0))
    return EFI_INVALID_PARAMETER;
  Status = InternalReadMachHeader(Context->Buffer, Context->BufferSize, 0, &Header);
  if (EFI_ERROR(Status))
    return Status;
  if (Header.FileType != AC_MACH_FILESET)
    return EFI_UNSUPPORTED;
  if (Header.CommandCount > AC_KERNEL_MAX_LOAD_COMMANDS ||
      Context->EntryCount > AC_KERNEL_MAX_LOAD_COMMANDS)
    return EFI_BAD_BUFFER_SIZE;
  Layout->Segments =
    AllocateZeroPool(MIN(Header.CommandCount, AC_KERNEL_MAX_SEGMENTS) * sizeof(Segment));
  if (Layout->Segments == NULL)
    return EFI_OUT_OF_RESOURCES;
  End = sizeof(Header) + Header.CommandsSize;
  Commands = Header.CommandCount;
  ZeroFillBudget = Context->BufferSize;
  for (Offset = sizeof(Header); Offset < End; Offset += Command.Size)
  {
    CopyMem(&Command, Context->Buffer + Offset, sizeof(Command));
    if (Command.Type == AC_MACH_LC_SEGMENT_64)
    {
      if (Layout->SegmentCount == AC_KERNEL_MAX_SEGMENTS)
        return EFI_BAD_BUFFER_SIZE;
      CopyMem(&Segment, Context->Buffer + Offset, sizeof(Segment));
      Status = CheckSegment(&Segment);
      if (EFI_ERROR(Status))
        return Status;
      if (CompareMem(Segment.Name, TextName, sizeof(TextName)) == 0)
      {
        if (HasText)
          return EFI_COMPROMISED_DATA;
        HasText = TRUE;
        TextSegment = Segment;
      }
      if (CompareMem(Segment.Name, LinkeditName, sizeof(LinkeditName)) == 0)
      {
        if (HasLinkedit)
          return EFI_COMPROMISED_DATA;
        HasLinkedit = TRUE;
      }
      if (Segment.VirtualSize == 0)
        continue;
      if ((Segment.VirtualAddress & (AC_KERNEL_PAGE_SIZE - 1)) != 0)
        return EFI_UNSUPPORTED;
      for (Index = 0; Index < Layout->SegmentCount; ++Index)
      {
        CONST AC_MACH_SEGMENT *Previous = &Layout->Segments[Index];
        if ((Segment.VirtualAddress < Previous->VirtualAddress + Previous->VirtualSize) &&
            (Previous->VirtualAddress < Segment.VirtualAddress + Segment.VirtualSize))
          return EFI_COMPROMISED_DATA;
      }
      Layout->Segments[Layout->SegmentCount++] = Segment;
      Low = MIN(Low, Segment.VirtualAddress);
      High = MAX(High, Segment.VirtualAddress + Segment.VirtualSize);
    }
    else if (Command.Type == AC_MACH_LC_UNIXTHREAD)
    {
      if (HasEntry || Command.Size != sizeof(Thread))
        return EFI_COMPROMISED_DATA;
      CopyMem(&Thread, Context->Buffer + Offset, sizeof(Thread));
      if ((Thread.Flavor != AC_ARM_THREAD_STATE64) || (Thread.Count != sizeof(Thread.State) / 4))
        return EFI_UNSUPPORTED;
      Layout->Info.EntryPoint = Thread.State.Pc;
      HasEntry = TRUE;
    }
    else if (Command.Type == AC_MACH_LC_MAIN)
      return EFI_UNSUPPORTED;
    else if (Command.Type == AC_MACH_LC_DYLD_CHAINED_FIXUPS)
    {
      if (HasFixups)
        return EFI_COMPROMISED_DATA;
      HasFixups = TRUE;
    }
  }
  if (!HasEntry || Layout->SegmentCount == 0)
    return EFI_UNSUPPORTED;
  if ((Layout->Info.EntryPoint & 3U) != 0)
    return EFI_COMPROMISED_DATA;
  if (!HasText || TextSegment.VirtualAddress != Low || TextSegment.FileOffset != 0 ||
      TextSegment.FileSize < End)
    return EFI_COMPROMISED_DATA;
  Span = High - Low;
  if ((Span > MAX_UINTN - (AC_KERNEL_PAGE_SIZE - 1)) ||
      (Low > MAX_UINT64 - ALIGN_VALUE(Span, AC_KERNEL_PAGE_SIZE)))
    return EFI_BAD_BUFFER_SIZE;
  Layout->Info.VirtualBase = Low;
  Layout->Info.MemorySize = (UINTN)ALIGN_VALUE(Span, AC_KERNEL_PAGE_SIZE);
  Layout->Info.Alignment = AC_KERNEL_BLOCK_SIZE;
  if (Layout->Info.MemorySize > MaxMemorySize)
    return EFI_BAD_BUFFER_SIZE;
  for (Offset = sizeof(Header); Offset < End; Offset += Command.Size)
  {
    CopyMem(&Command, Context->Buffer + Offset, sizeof(Command));
    Status = CheckLinkedit(Context->Buffer, Offset, &Command, Layout);
    if (EFI_ERROR(Status))
      return Status;
  }
  for (Index = 0; Index < Context->EntryCount; ++Index)
  {
    CONST AC_KERNEL_COLLECTION_ENTRY *Entry = &Context->Entries[Index];
    Status =
      InternalReadMachHeader(Context->Buffer, Context->BufferSize, Entry->FileOffset, &ChildHeader);
    if (EFI_ERROR(Status))
      return Status;
    if (ChildHeader.CommandCount > AC_KERNEL_MAX_LOAD_COMMANDS - Commands)
      return EFI_BAD_BUFFER_SIZE;
    Commands += ChildHeader.CommandCount;
    ChildOffset = Entry->FileOffset + sizeof(ChildHeader);
    ChildEnd = ChildOffset + ChildHeader.CommandsSize;
    if (!FileRangeIsMapped(Layout, Entry->FileOffset, ChildEnd - Entry->FileOffset,
                           Entry->VirtualAddress))
      return EFI_COMPROMISED_DATA;
    if (Index == Context->KernelIndex)
    {
      if ((ChildHeader.Flags & AC_MACH_DYLIB_IN_CACHE) == 0)
        return EFI_UNSUPPORTED;
      if (ChildEnd - ChildOffset < sizeof(Segment))
        return EFI_COMPROMISED_DATA;
      CopyMem(&Segment, Context->Buffer + ChildOffset, sizeof(Segment));
      if (Segment.Command.Type != AC_MACH_LC_SEGMENT_64 || Segment.Command.Size < sizeof(Segment) ||
          CompareMem(Segment.Name, TextName, sizeof(TextName)) != 0 ||
          Segment.VirtualAddress != Entry->VirtualAddress ||
          Segment.FileOffset != Entry->FileOffset ||
          Segment.FileSize < ChildEnd - Entry->FileOffset)
        return EFI_COMPROMISED_DATA;
    }
    for (; ChildOffset < ChildEnd; ChildOffset += Command.Size)
    {
      CopyMem(&Command, Context->Buffer + ChildOffset, sizeof(Command));
      if (Command.Type == AC_MACH_LC_SEGMENT_64)
      {
        CopyMem(&Segment, Context->Buffer + ChildOffset, sizeof(Segment));
        Status = CheckEmbeddedSegment(Context->Buffer, &Segment, Layout, &ZeroFillBudget);
        if (EFI_ERROR(Status))
          return Status;
        if (Index == Context->KernelIndex &&
            (Segment.InitialProtection & AC_MACH_VM_PROT_EXECUTE) != 0 &&
            Layout->Info.EntryPoint >= Segment.VirtualAddress &&
            Layout->Info.EntryPoint - Segment.VirtualAddress < Segment.FileSize &&
            Segment.FileSize - (Layout->Info.EntryPoint - Segment.VirtualAddress) >= 4)
          EntryInKernel = TRUE;
      }
      Status = CheckLinkedit(Context->Buffer, ChildOffset, &Command, Layout);
      if (EFI_ERROR(Status))
        return Status;
    }
  }
  for (Other = 0; Other < Layout->SegmentCount; ++Other)
  {
    CONST AC_MACH_SEGMENT *Executable = &Layout->Segments[Other];
    if (EntryInKernel && (Executable->InitialProtection & AC_MACH_VM_PROT_EXECUTE) != 0 &&
        Layout->Info.EntryPoint >= Executable->VirtualAddress &&
        Layout->Info.EntryPoint - Executable->VirtualAddress < Executable->FileSize &&
        Executable->FileSize - (Layout->Info.EntryPoint - Executable->VirtualAddress) >= 4)
      return EFI_SUCCESS;
  }
  return EFI_COMPROMISED_DATA;
}

EFI_STATUS
AcKernelGetLoadInfo(IN CONST AC_KERNEL_COLLECTION_CONTEXT *Context, IN UINTN MaxMemorySize,
                    OUT AC_KERNEL_LOAD_INFO *Info)
{
  AC_KERNEL_LAYOUT Layout;
  EFI_STATUS       Status;

  if (Info == NULL)
    return EFI_INVALID_PARAMETER;
  ZeroMem(Info, sizeof(*Info));
  Status = ReadLayout(Context, MaxMemorySize, &Layout);
  if (!EFI_ERROR(Status))
    *Info = Layout.Info;
  if (Layout.Segments != NULL)
    FreePool(Layout.Segments);
  return Status;
}

EFI_STATUS
AcKernelLoadCollection(IN CONST AC_KERNEL_COLLECTION_CONTEXT *Context, OUT VOID *Destination,
                       IN UINTN Capacity, IN EFI_PHYSICAL_ADDRESS PhysicalBase,
                       OUT AC_KERNEL_LOADED_IMAGE *Image)
{
  AC_KERNEL_LAYOUT Layout;
  EFI_STATUS       Status;
  UINTN            Size;
  UINTN            Source;
  UINTN            Target;
  UINT32           Index;

  if (Image == NULL)
    return EFI_INVALID_PARAMETER;
  ZeroMem(Image, sizeof(*Image));
  if (Destination == NULL)
    return EFI_INVALID_PARAMETER;
  Status = ReadLayout(Context, MAX_UINTN, &Layout);
  if (EFI_ERROR(Status))
    goto Done;
  Size = Layout.Info.MemorySize;
  Source = (UINTN)Context->Buffer;
  Target = (UINTN)Destination;
  if (Capacity < Size)
  {
    Status = EFI_BUFFER_TOO_SMALL;
    goto Done;
  }
  if ((PhysicalBase > MAX_UINT64 - Size) ||
      ((PhysicalBase ^ Layout.Info.VirtualBase) & (Layout.Info.Alignment - 1)) != 0 ||
      (Source > MAX_UINTN - Context->BufferSize) || (Target > MAX_UINTN - Size) ||
      (Source < Target + Size && Target < Source + Context->BufferSize))
  {
    Status = EFI_INVALID_PARAMETER;
    goto Done;
  }
  ZeroMem(Destination, Size);
  for (Index = 0; Index < Layout.SegmentCount; ++Index)
  {
    CONST AC_MACH_SEGMENT *Segment = &Layout.Segments[Index];
    CopyMem((UINT8 *)Destination + (UINTN)(Segment->VirtualAddress - Layout.Info.VirtualBase),
            Context->Buffer + (UINTN)Segment->FileOffset, (UINTN)Segment->FileSize);
  }
  Image->Buffer = Destination;
  Image->PhysicalBase = PhysicalBase;
  Image->EntryPoint = PhysicalBase + (Layout.Info.EntryPoint - Layout.Info.VirtualBase);
  Image->Info = Layout.Info;
Done:
  if (Layout.Segments != NULL)
    FreePool(Layout.Segments);
  return Status;
}
