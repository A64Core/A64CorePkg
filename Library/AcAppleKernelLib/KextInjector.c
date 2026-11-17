/** @file
  Transactional, ordered Boot KC injection and collection metadata.
**/
#include "AppleKernelInternal.h"

CONST AC_PLIST_NODE *
InternalKextProperty(CONST AC_PLIST_NODE *Dictionary, CONST CHAR8 *Name)
{
  UINTN                Index;
  CONST CHAR8         *Key;
  CONST AC_PLIST_NODE *Value;
  if (AcPlistGetType(Dictionary) != AcPlistTypeDictionary)
    return NULL;
  for (Index = 0; Index < AcPlistGetCount(Dictionary); ++Index)
  {
    AcPlistGetDictionaryItem(Dictionary, Index, &Key, &Value);
    if (AsciiStrCmp(Key, Name) == 0)
      return Value;
  }
  return NULL;
}

STATIC EFI_STATUS
ReserveUsed(AC_KEXT_CONTEXT *Context, UINTN Offset, UINTN Size)
{
  AC_KERNEL_FILE_RANGE *Ranges;
  UINTN                 Capacity;
  if (Size == 0)
    return EFI_SUCCESS;
  if (!KextRange(Context->Capacity, Offset, Size))
    return EFI_COMPROMISED_DATA;
  if (Context->UsedCount == Context->UsedCapacity)
  {
    Capacity = Context->UsedCapacity == 0 ? 64 : Context->UsedCapacity * 2;
    if (Capacity > AC_KEXT_LIMIT / sizeof(*Ranges))
      return EFI_BAD_BUFFER_SIZE;
    Ranges = AllocateZeroPool(Capacity * sizeof(*Ranges));
    if (Ranges == NULL)
      return EFI_OUT_OF_RESOURCES;
    if (Context->Used != NULL)
    {
      CopyMem(Ranges, Context->Used, Context->UsedCount * sizeof(*Ranges));
      FreePool(Context->Used);
    }
    Context->Used = Ranges;
    Context->UsedCapacity = Capacity;
  }
  Context->Used[Context->UsedCount].Offset = Offset;
  Context->Used[Context->UsedCount++].Size = Size;
  return EFI_SUCCESS;
}

EFI_STATUS
InternalKextAllocate(AC_KEXT_CONTEXT *Context, CONST CHAR8 *RootName, UINTN Size, UINTN Alignment,
                     UINTN *Offset, UINT64 *Address)
{
  UINTN            RootIndex;
  UINTN            At;
  UINTN            Next;
  UINTN            Index;
  UINTN            Byte;
  UINTN            End;
  AC_MACH_SEGMENT *Root;
  EFI_STATUS       Status;
  if (Size == 0 || Alignment == 0 || (Alignment & (Alignment - 1)) != 0)
    return EFI_INVALID_PARAMETER;
  for (RootIndex = 0; RootIndex < Context->RootCount; ++RootIndex)
    if (KextName(Context->Roots[RootIndex].Mach.Name, RootName))
      break;
  if (RootIndex == Context->RootCount)
    return EFI_UNSUPPORTED;
  Root = &Context->Roots[RootIndex].Mach;
  if ((Root->InitialProtection & 1U) == 0 ||
      (AsciiStrCmp(RootName, "__TEXT_EXEC") == 0 && (Root->InitialProtection & 4U) == 0) ||
      (AsciiStrCmp(RootName, "__DATA") == 0 && (Root->InitialProtection & 3U) != 3U))
    return EFI_UNSUPPORTED;
  if (RootIndex == Context->LinkRoot)
  {
    // LINKEDIT may grow at the collection's end without moving any old address.
    if (Context->Size < Root->FileOffset)
      return EFI_UNSUPPORTED;
    At = (UINTN)Root->FileOffset + ALIGN_VALUE(Context->Size - (UINTN)Root->FileOffset, Alignment);
    if (At < Root->FileOffset || !KextRange(Context->Capacity, At, Size))
      return EFI_OUT_OF_RESOURCES;
    if (Root->VirtualAddress > MAX_UINT64 - (At - Root->FileOffset) - Size)
      return EFI_BAD_BUFFER_SIZE;
    for (Index = 0; Index < Context->RootCount; ++Index)
      if (Index != RootIndex &&
          Context->Roots[Index].Mach.VirtualAddress + Context->Roots[Index].Mach.VirtualSize >
            Root->VirtualAddress)
        return EFI_UNSUPPORTED;
    Root->FileSize = At + Size - Root->FileOffset;
    Root->VirtualSize = ALIGN_VALUE(Root->FileSize, AC_KEXT_PAGE);
    Context->Size = At + Size;
  }
  else
  {
    At = (UINTN)Root->FileOffset;
    End = (UINTN)(Root->FileOffset + Root->FileSize);
    if (AsciiStrCmp(RootName, "__TEXT") == 0)
      End = MIN(End, Context->HeaderLimit);
    for (;;)
    {
      if (!KextRange(End, At, Size))
        return EFI_OUT_OF_RESOURCES;
      Next = At;
      for (Index = 0; Index < Context->UsedCount; ++Index)
        if (At < Context->Used[Index].Offset + Context->Used[Index].Size &&
            Context->Used[Index].Offset < At + Size)
          Next = MAX(Next, Context->Used[Index].Offset + Context->Used[Index].Size);
      if (Next != At)
      {
        At = (UINTN)Root->FileOffset + ALIGN_VALUE(Next - (UINTN)Root->FileOffset, Alignment);
        continue;
      }
      for (Byte = 0; Byte < Size; ++Byte)
        if (Context->Buffer[At + Byte] != 0)
          break;
      if (Byte == Size)
        break;
      At =
        (UINTN)Root->FileOffset + ALIGN_VALUE(At + Byte + 1 - (UINTN)Root->FileOffset, Alignment);
    }
  }
  Status = ReserveUsed(Context, At, Size);
  if (EFI_ERROR(Status))
    return Status;
  *Offset = At;
  *Address = Root->VirtualAddress + At - Root->FileOffset;
  return EFI_SUCCESS;
}

EFI_STATUS
InternalKextAddFixup(AC_KEXT_CONTEXT *Context, UINTN Offset, UINT64 Body)
{
  AC_KEXT_FIXUP *Fixups;
  UINTN          Capacity;
  if ((Offset & 3U) != 0 || !KextRange(Context->Size, Offset, 8))
    return EFI_COMPROMISED_DATA;
  if (Context->FixupCount == Context->FixupCapacity)
  {
    Capacity = Context->FixupCapacity == 0 ? 256 : Context->FixupCapacity * 2;
    if (Capacity > AC_KEXT_LIMIT / sizeof(*Fixups))
      return EFI_BAD_BUFFER_SIZE;
    Fixups = AllocateZeroPool(Capacity * sizeof(*Fixups));
    if (Fixups == NULL)
      return EFI_OUT_OF_RESOURCES;
    if (Context->Fixups != NULL)
    {
      CopyMem(Fixups, Context->Fixups, Context->FixupCount * sizeof(*Fixups));
      FreePool(Context->Fixups);
    }
    Context->Fixups = Fixups;
    Context->FixupCapacity = Capacity;
  }
  Context->Fixups[Context->FixupCount].Offset = Offset;
  Context->Fixups[Context->FixupCount++].Body = Body;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ReadRootChains(AC_KEXT_CONTEXT *Context)
{
  CONST UINT8     *Blob;
  CONST UINT8     *Starts;
  CONST UINT8     *Record;
  UINTN            Size;
  UINTN            StartOffset;
  UINTN            Relative;
  UINTN            RecordSize;
  UINTN            Index;
  UINTN            Page;
  UINTN            Pages;
  UINTN            Site;
  UINTN            Next;
  UINTN            Start;
  UINT64           Word;
  AC_MACH_SEGMENT *Root;
  EFI_STATUS       Status;
  UINTN            Command = Context->ChainsCommand;
  if (Command == 0)
    return EFI_UNSUPPORTED;
  Size = KextRead32(Context->Buffer + Command + 12);
  Relative = KextRead32(Context->Buffer + Command + 8);
  if (!KextRange(Context->Size, Relative, Size) || Size < 28)
    return EFI_COMPROMISED_DATA;
  Blob = Context->Buffer + Relative;
  if (KextRead32(Blob) != 0 || KextRead32(Blob + 16) != 0 || KextRead32(Blob + 24) != 0)
    return EFI_UNSUPPORTED;
  StartOffset = KextRead32(Blob + 4);
  if (StartOffset < 28 || !KextRange(Size, StartOffset, 4 + Context->RootCount * 4))
    return EFI_COMPROMISED_DATA;
  Starts = Blob + StartOffset;
  if (KextRead32(Starts) != Context->RootCount)
    return EFI_COMPROMISED_DATA;
  Status = ReserveUsed(Context, Relative, Size);
  if (EFI_ERROR(Status))
    return Status;
  for (Index = 0; Index < Context->RootCount; ++Index)
  {
    Relative = KextRead32(Starts + 4 + Index * 4);
    if (Relative == 0)
      continue;
    if (!KextRange(Size - StartOffset, Relative, 22))
      return EFI_COMPROMISED_DATA;
    Record = Starts + Relative;
    RecordSize = KextRead32(Record);
    Pages = KextRead16(Record + 20);
    Root = &Context->Roots[Index].Mach;
    if (RecordSize < 22 + Pages * 2 || !KextRange(Size - StartOffset, Relative, RecordSize))
      return EFI_COMPROMISED_DATA;
    if (KextRead16(Record + 4) != AC_KEXT_PAGE || KextRead16(Record + 6) != 8 ||
        KextRead64(Record + 8) != Root->VirtualAddress - Context->Base ||
        Pages > ALIGN_VALUE(Root->VirtualSize, AC_KEXT_PAGE) / AC_KEXT_PAGE)
      return EFI_UNSUPPORTED;
    for (Page = 0; Page < Pages; ++Page)
    {
      Start = KextRead16(Record + 22 + Page * 2);
      if (Start == 0xFFFFU)
        continue;
      if ((Start & 0x8000U) != 0)
        return EFI_UNSUPPORTED;
      Site = Page * AC_KEXT_PAGE + Start;
      for (;;)
      {
        if (Site < Page * AC_KEXT_PAGE || Site >= (Page + 1) * AC_KEXT_PAGE ||
            !KextRange(Root->FileSize, Site, 8))
          return EFI_COMPROMISED_DATA;
        Word = KextRead64(Context->Buffer + Root->FileOffset + Site);
        Next = (UINTN)((Word >> 51) & 0xFFFU);
        Status =
          InternalKextAddFixup(Context, (UINTN)Root->FileOffset + Site, Word & ~(0xFFFULL << 51));
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

STATIC VOID
SiftFixups(AC_KEXT_FIXUP *Items, UINTN Count, UINTN Root)
{
  AC_KEXT_FIXUP Value = Items[Root];
  UINTN         Child;
  while (Root < Count / 2)
  {
    Child = Root * 2 + 1;
    if (Child + 1 < Count && Items[Child].Offset < Items[Child + 1].Offset)
      ++Child;
    if (Value.Offset >= Items[Child].Offset)
      break;
    Items[Root] = Items[Child];
    Root = Child;
  }
  Items[Root] = Value;
}

STATIC EFI_STATUS
WriteRootChains(AC_KEXT_CONTEXT *Context)
{
  UINTN            Index;
  UINTN            Other;
  UINTN            Pages;
  UINTN            Size = 28 + 4 + Context->RootCount * 4;
  UINTN            Offset;
  UINTN            Cursor;
  UINTN            Record;
  UINTN            Site;
  UINTN            Previous;
  UINTN            PreviousPage;
  UINTN            Page;
  UINTN            Delta;
  UINT64           Address;
  AC_KEXT_FIXUP    Value;
  AC_MACH_SEGMENT *Root;
  EFI_STATUS       Status;
  for (Index = Context->FixupCount / 2; Index > 0; --Index)
    SiftFixups(Context->Fixups, Context->FixupCount, Index - 1);
  for (Index = Context->FixupCount; Index > 1; --Index)
  {
    Value = Context->Fixups[0];
    Context->Fixups[0] = Context->Fixups[Index - 1];
    Context->Fixups[Index - 1] = Value;
    SiftFixups(Context->Fixups, Index - 1, 0);
  }
  for (Index = 1; Index < Context->FixupCount; ++Index)
    if (Context->Fixups[Index].Offset < Context->Fixups[Index - 1].Offset + 8)
      return EFI_COMPROMISED_DATA;
  for (Index = 0; Index < Context->RootCount; ++Index)
  {
    Pages = ALIGN_VALUE(Context->Roots[Index].Mach.VirtualSize, AC_KEXT_PAGE) / AC_KEXT_PAGE;
    if (Pages > MAX_UINT16)
      return EFI_BAD_BUFFER_SIZE;
    Size += 22 + Pages * 2;
  }
  Status = InternalKextAllocate(Context, "__LINKEDIT", Size, 8, &Offset, &Address);
  if (EFI_ERROR(Status))
    return Status;
  // Allocation can increase LINKEDIT's page count.
  Root = &Context->Roots[Context->LinkRoot].Mach;
  Pages = ALIGN_VALUE(Root->VirtualSize, AC_KEXT_PAGE) / AC_KEXT_PAGE;
  if (Pages > MAX_UINT16)
    return EFI_BAD_BUFFER_SIZE;
  Cursor = 28 + 4 + Context->RootCount * 4;
  ZeroMem(Context->Buffer + Offset, Size);
  KextWrite32(Context->Buffer + Offset + 4, 28);
  KextWrite32(Context->Buffer + Offset + 8, (UINT32)Size);
  KextWrite32(Context->Buffer + Offset + 12, (UINT32)Size);
  KextWrite32(Context->Buffer + Offset + 20, 1);
  KextWrite32(Context->Buffer + Offset + 28, (UINT32)Context->RootCount);
  for (Index = 0; Index < Context->RootCount; ++Index)
  {
    Root = &Context->Roots[Index].Mach;
    Previous = MAX_UINTN;
    PreviousPage = MAX_UINTN;
    Record = Cursor;
    Pages = ALIGN_VALUE(Root->VirtualSize, AC_KEXT_PAGE) / AC_KEXT_PAGE;
    // There are no pointer chains in LINKEDIT; omit its record.
    if (Index == Context->LinkRoot)
      continue;
    if (!KextRange(Size, Record, 22 + Pages * 2))
      return EFI_BAD_BUFFER_SIZE;
    KextWrite32(Context->Buffer + Offset + 28 + 4 + Index * 4, (UINT32)(Record - 28));
    KextWrite32(Context->Buffer + Offset + Record, (UINT32)(22 + Pages * 2));
    KextWrite32(Context->Buffer + Offset + Record + 4, AC_KEXT_PAGE | (8U << 16));
    KextWrite64(Context->Buffer + Offset + Record + 8, Root->VirtualAddress - Context->Base);
    {
      UINT16 PageCount = (UINT16)Pages;
      CopyMem(Context->Buffer + Offset + Record + 20, &PageCount, 2);
    }
    for (Other = 0; Other < Pages * 2; ++Other)
      Context->Buffer[Offset + Record + 22 + Other] = 0xFF;
    for (Other = 0; Other < Context->FixupCount; ++Other)
    {
      Site = Context->Fixups[Other].Offset;
      if (Site < Root->FileOffset || Site >= Root->FileOffset + Root->FileSize)
        continue;
      Page = (Site - Root->FileOffset) / AC_KEXT_PAGE;
      if (Page >= Pages)
        return EFI_COMPROMISED_DATA;
      KextWrite64(Context->Buffer + Site, Context->Fixups[Other].Body);
      if (Page != PreviousPage)
      {
        UINT16 Start = (UINT16)((Site - Root->FileOffset) % AC_KEXT_PAGE);
        CopyMem(Context->Buffer + Offset + Record + 22 + Page * 2, &Start, 2);
      }
      else
      {
        Delta = Site - Previous;
        if ((Delta & 3U) != 0 || Delta / 4 > 0xFFFU)
          return EFI_UNSUPPORTED;
        KextWrite64(Context->Buffer + Previous,
                    KextRead64(Context->Buffer + Previous) | ((UINT64)(Delta / 4) << 51));
      }
      Previous = Site;
      PreviousPage = Page;
    }
    Cursor += 22 + Pages * 2;
  }
  KextWrite32(Context->Buffer + Context->ChainsCommand + 8, (UINT32)Offset);
  KextWrite32(Context->Buffer + Context->ChainsCommand + 12, (UINT32)Size);
  return EFI_SUCCESS;
}

STATIC CONST AC_PLIST_NODE *
FindInfo(AC_KEXT_CONTEXT *Context, CONST CHAR8 *Identifier, UINTN Before)
{
  UINTN                Index;
  CONST AC_PLIST_NODE *Node;
  CONST CHAR8         *Name;
  for (Index = 0; Index < AcPlistGetCount(Context->Prelink); ++Index)
  {
    Node = AcPlistGetArrayItem(Context->Prelink, Index);
    Name = AcPlistGetString(InternalKextProperty(Node, "CFBundleIdentifier"));
    if (Name != NULL && AsciiStrCmp(Name, Identifier) == 0)
      return Node;
  }
  for (Index = 0; Index < Before; ++Index)
    if (AsciiStrCmp(Context->Images[Index].Identifier, Identifier) == 0)
      return Context->Images[Index].Info;
  return NULL;
}

STATIC BOOLEAN
BundleVersion(CONST CHAR8 *Text, UINT64 *Value)
{
  UINT64 Parts = 0;
  UINTN  Index;
  UINTN  Part;
  UINTN  Stage = 4;
  UINTN  Build = 0;
  if (Text == NULL || *Text == '\0')
    return FALSE;
  for (Index = 0; Index < 3; ++Index)
  {
    Part = 0;
    if (*Text < '0' || *Text > '9')
      return FALSE;
    do
    {
      Part = Part * 10 + (UINTN)(*Text++ - '0');
      if (Part > 9999)
        return FALSE;
    } while (*Text >= '0' && *Text <= '9');
    Parts = Parts * 10000 + Part;
    if (*Text != '.')
    {
      while (++Index < 3)
        Parts *= 10000;
      break;
    }
    ++Text;
  }
  if (*Text != '\0')
  {
    if (*Text == 'd')
      Stage = 0;
    else if (*Text == 'a')
      Stage = 1;
    else if (*Text == 'b')
      Stage = 2;
    else if (*Text == 'f' && Text[1] == 'c')
    {
      Stage = 3;
      ++Text;
    }
    else
      return FALSE;
    ++Text;
    if (*Text < '0' || *Text > '9')
      return FALSE;
    do
    {
      Build = Build * 10 + (UINTN)(*Text++ - '0');
      if (Build > 255)
        return FALSE;
    } while (*Text >= '0' && *Text <= '9');
    if (*Text != '\0')
      return FALSE;
  }
  *Value = (Parts << 12) | (Stage << 8) | Build;
  return TRUE;
}

STATIC EFI_STATUS
CheckLibraries(AC_KEXT_CONTEXT *Context, UINTN Index)
{
  AC_KEXT_IMAGE       *Image = &Context->Images[Index];
  CONST AC_PLIST_NODE *Provider;
  CONST AC_PLIST_NODE *Required;
  CONST CHAR8         *Name;
  CONST CHAR8         *Text;
  UINT64               Need;
  UINT64               Have;
  UINT64               Compatible;
  UINTN                Field;
  for (Field = 0; Field < AcPlistGetCount(Image->Libraries); ++Field)
  {
    AcPlistGetDictionaryItem(Image->Libraries, Field, &Name, &Required);
    Provider = FindInfo(Context, Name, Index);
    if (Provider == NULL)
      return EFI_NOT_FOUND;
    if (!BundleVersion(AcPlistGetString(Required), &Need) ||
        !BundleVersion(AcPlistGetString(InternalKextProperty(Provider, "CFBundleVersion")), &Have))
      return EFI_COMPROMISED_DATA;
    Text = AcPlistGetString(InternalKextProperty(Provider, "OSBundleCompatibleVersion"));
    if (Text == NULL)
      Compatible = Have;
    else if (!BundleVersion(Text, &Compatible))
      return EFI_COMPROMISED_DATA;
    if (Compatible > Have || Need < Compatible || Need > Have)
      return EFI_INCOMPATIBLE_VERSION;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
PrepareSource(AC_KEXT_CONTEXT *Context)
{
  AC_MACH_HEADER  Header;
  AC_MACH_HEADER  Child;
  AC_MACH_COMMAND Command;
  AC_MACH_SEGMENT Segment;
  AC_KEXT_ROOT   *Root;
  EFI_STATUS      Status;
  UINTN           Offset;
  UINTN           At;
  UINTN           Index;
  UINTN           ParentIndex;
  UINTN           Size;
  UINTN           Section;
  UINT64          Mapped;
  Context->Base = MAX_UINT64;
  Context->InfoRoot = Context->LinkRoot = MAX_UINTN;
  Status = InternalReadMachHeader(Context->Buffer, Context->Size, 0, &Header);
  if (EFI_ERROR(Status))
    return Status;
  Context->HeaderEnd = sizeof(Header) + Header.CommandsSize;
  Context->HeaderLimit = Context->Size;
  for (Offset = sizeof(Header); Offset < Context->HeaderEnd; Offset += Command.Size)
  {
    CopyMem(&Command, Context->Buffer + Offset, sizeof(Command));
    if (Command.Type == AC_MACH_LC_SEGMENT_64)
    {
      if (Context->RootCount == AC_KEXT_ROOTS)
        return EFI_BAD_BUFFER_SIZE;
      Root = &Context->Roots[Context->RootCount];
      CopyMem(&Root->Mach, Context->Buffer + Offset, sizeof(Root->Mach));
      Root->Command = Offset;
      if ((Root->Mach.VirtualAddress & (AC_KEXT_PAGE - 1)) != 0 ||
          Root->Mach.FileSize > Root->Mach.VirtualSize)
        return EFI_UNSUPPORTED;
      Context->Base = MIN(Context->Base, Root->Mach.VirtualAddress);
      if (KextName(Root->Mach.Name, "__PRELINK_INFO"))
        Context->InfoRoot = Context->RootCount;
      if (KextName(Root->Mach.Name, "__LINKEDIT"))
        Context->LinkRoot = Context->RootCount;
      for (Section = 0; Section < Root->Mach.SectionCount; ++Section)
      {
        At = KextRead32(Context->Buffer + Offset + 72 + Section * 80 + 48);
        if (At >= Context->HeaderEnd && At != 0)
          Context->HeaderLimit = MIN(Context->HeaderLimit, At);
      }
      ++Context->RootCount;
    }
    else if (Command.Type == AC_MACH_LC_DYLD_CHAINED_FIXUPS)
    {
      if (Command.Size != 16 || Context->ChainsCommand != 0)
        return EFI_COMPROMISED_DATA;
      Context->ChainsCommand = Offset;
    }
  }
  if (Context->InfoRoot == MAX_UINTN || Context->LinkRoot == MAX_UINTN)
    return EFI_UNSUPPORTED;
  for (Index = 0; Index < Context->Source->EntryCount; ++Index)
  {
    Offset = Context->Source->Entries[Index].FileOffset;
    Context->HeaderLimit = MIN(Context->HeaderLimit, Offset);
    Status = InternalReadMachHeader(Context->Buffer, Context->Size, Offset, &Child);
    if (EFI_ERROR(Status))
      return Status;
    for (At = Offset + sizeof(Child); At < Offset + sizeof(Child) + Child.CommandsSize;
         At += Command.Size)
    {
      CopyMem(&Command, Context->Buffer + At, sizeof(Command));
      if (Command.Type != AC_MACH_LC_SEGMENT_64)
        continue;
      CopyMem(&Segment, Context->Buffer + At, sizeof(Segment));
      if (Segment.VirtualSize == 0)
        continue;
      // Reserve full member virtual ranges, including zero-fill and padding.
      for (ParentIndex = 0; ParentIndex < Context->RootCount; ++ParentIndex)
      {
        AC_MACH_SEGMENT *Parent = &Context->Roots[ParentIndex].Mach;
        UINT64           Delta;
        if (Segment.VirtualAddress < Parent->VirtualAddress)
          continue;
        Delta = Segment.VirtualAddress - Parent->VirtualAddress;
        if (!KextRange((UINTN)Parent->VirtualSize, Delta, Segment.VirtualSize))
          continue;
        Mapped = Delta >= Parent->FileSize ? 0 : MIN(Segment.VirtualSize, Parent->FileSize - Delta);
        Status = ReserveUsed(Context, (UINTN)(Parent->FileOffset + Delta), (UINTN)Mapped);
        if (EFI_ERROR(Status))
          return Status;
        break;
      }
      if (ParentIndex == Context->RootCount)
        return EFI_UNSUPPORTED;
    }
  }
  Status = ReserveUsed(Context, 0, Context->HeaderEnd);
  if (EFI_ERROR(Status))
    return Status;
  Root = &Context->Roots[Context->InfoRoot];
  Size = (UINTN)Root->Mach.FileSize;
  for (Index = 0; Index < Size && Context->Buffer[Root->Mach.FileOffset + Index] != 0; ++Index)
  {
  }
  if (Index == Size)
    return EFI_COMPROMISED_DATA;
  Status = AcPlistParse(Context->Buffer + Root->Mach.FileOffset, Index, &Context->Document);
  if (EFI_ERROR(Status))
    return Status;
  Context->Prelink =
    InternalKextProperty(AcPlistGetRoot(Context->Document), "_PrelinkInfoDictionary");
  if (AcPlistGetType(Context->Prelink) != AcPlistTypeArray)
    return EFI_COMPROMISED_DATA;
  return ReadRootChains(Context);
}

typedef struct
{
  CHAR8     *Text;
  UINTN      Used;
  UINTN      Capacity;
  EFI_STATUS Status;
} KEXT_XML;

STATIC VOID
AppendXml(KEXT_XML *Xml, CONST CHAR8 *Text, UINTN Size)
{
  if (EFI_ERROR(Xml->Status))
    return;
  if (Size > Xml->Capacity - Xml->Used)
  {
    Xml->Status = EFI_BAD_BUFFER_SIZE;
    return;
  }
  CopyMem(Xml->Text + Xml->Used, Text, Size);
  Xml->Used += Size;
}

STATIC VOID
AppendText(KEXT_XML *Xml, CONST CHAR8 *Text)
{
  AppendXml(Xml, Text, AsciiStrLen(Text));
}

STATIC VOID
AppendEscaped(KEXT_XML *Xml, CONST CHAR8 *Text)
{
  for (; *Text != '\0'; ++Text)
  {
    if (*Text == '&')
      AppendText(Xml, "&amp;");
    else if (*Text == '<')
      AppendText(Xml, "&lt;");
    else if (*Text == '>')
      AppendText(Xml, "&gt;");
    else if (*Text == '\\')
      AppendText(Xml, "/");
    else
      AppendXml(Xml, Text, 1);
  }
}

STATIC VOID
AppendInteger(KEXT_XML *Xml, CONST CHAR8 *Key, UINT64 Value)
{
  CHAR8 Digits[21];
  UINTN Used = sizeof(Digits);
  // XNU accepts unslid kernel addresses as signed decimal plist integers.
  BOOLEAN Negative = (Value >> 63) != 0;
  if (Negative)
    Value = (~Value) + 1;
  do
  {
    Digits[--Used] = (CHAR8)('0' + Value % 10);
    Value /= 10;
  } while (Value != 0);
  AppendText(Xml, "<key>");
  AppendText(Xml, Key);
  AppendText(Xml, "</key><integer>");
  if (Negative)
    AppendText(Xml, "-");
  AppendXml(Xml, Digits + Used, sizeof(Digits) - Used);
  AppendText(Xml, "</integer>");
}

STATIC EFI_STATUS
BuildMetadata(AC_KEXT_CONTEXT *Context, KEXT_XML *Xml)
{
  CHAR8               *Base = NULL;
  CHAR8               *Part = NULL;
  UINTN                Size;
  UINTN                Index;
  UINTN                Field;
  CONST AC_PLIST_NODE *Node;
  CONST AC_PLIST_NODE *Value;
  CONST CHAR8         *Key;
  EFI_STATUS           Status;
  AppendText(Xml, "<plist version=\"1.0\"><dict>");
  Node = AcPlistGetRoot(Context->Document);
  for (Field = 0; Field < AcPlistGetCount(Node); ++Field)
  {
    AcPlistGetDictionaryItem(Node, Field, &Key, &Value);
    AppendText(Xml, "<key>");
    AppendEscaped(Xml, Key);
    AppendText(Xml, "</key>");
    if (AsciiStrCmp(Key, "_PrelinkInfoDictionary") != 0)
    {
      Status = AcPlistSerialize(Value, &Part, &Size);
      if (EFI_ERROR(Status))
        return Status;
      AppendXml(Xml, Part, Size);
      FreePool(Part);
      Part = NULL;
      continue;
    }
    Status = AcPlistSerialize(Value, &Base, &Size);
    if (EFI_ERROR(Status))
      return Status;
    if (Size < 15)
    {
      FreePool(Base);
      return EFI_COMPROMISED_DATA;
    }
    AppendXml(Xml, Base, Size - 8);
    FreePool(Base);
    Base = NULL;
    for (Index = 0; Index < Context->Count; ++Index)
    {
      AC_KEXT_IMAGE *Image = &Context->Images[Index];
      Status = AcPlistSerialize(Image->Info, &Part, &Size);
      if (EFI_ERROR(Status))
        return Status;
      if (Size < 13)
      {
        FreePool(Part);
        return EFI_COMPROMISED_DATA;
      }
      AppendXml(Xml, Part, Size - 7);
      FreePool(Part);
      Part = NULL;
      AppendText(Xml, "<key>_PrelinkBundlePath</key><string>/Library/Extensions/");
      AppendEscaped(Xml, Context->Kexts[Index].BundlePath);
      AppendText(Xml, "</string>");
      if (Image->Input != NULL)
      {
        AppendText(Xml, "<key>_PrelinkExecutableRelativePath</key><string>");
        AppendEscaped(Xml, Context->Kexts[Index].ExecutablePath);
        AppendText(Xml, "</string>");
        AppendInteger(Xml, "_PrelinkExecutableLoadAddr", Image->HeaderAddress);
        AppendInteger(Xml, "_PrelinkExecutableSourceAddr", Image->HeaderAddress);
        AppendInteger(Xml, "_PrelinkExecutableSize", Image->TextSize);
        AppendInteger(Xml, "_PrelinkKmodInfo", Image->Kmod);
      }
      AppendText(Xml, "</dict>");
    }
    AppendText(Xml, "</array>");
  }
  AppendText(Xml, "</dict></plist>");
  return Xml->Status;
}

STATIC EFI_STATUS
KernelSegment(AC_KEXT_CONTEXT *Context, CONST CHAR8 *Name, AC_MACH_SEGMENT *Segment,
              UINTN *CommandOffset)
{
  UINTN           Header = Context->Source->Entries[Context->Source->KernelIndex].FileOffset;
  UINTN           End = Header + 32 + KextRead32(Context->Buffer + Header + 20);
  UINTN           At;
  BOOLEAN         Found = FALSE;
  AC_MACH_COMMAND Command;
  for (At = Header + 32; At < End; At += Command.Size)
  {
    CopyMem(&Command, Context->Buffer + At, sizeof(Command));
    if (Command.Type != AC_MACH_LC_SEGMENT_64 ||
        !KextName((CONST CHAR8 *)Context->Buffer + At + 8, Name))
      continue;
    if (Found)
      return EFI_UNSUPPORTED;
    CopyMem(Segment, Context->Buffer + At, sizeof(*Segment));
    *CommandOffset = At;
    Found = TRUE;
  }
  return Found ? EFI_SUCCESS : EFI_NOT_FOUND;
}

STATIC BOOLEAN
EncodeCall(UINT64 From, UINT64 To, UINT32 *Instruction)
{
  INT64 Delta = (INT64)(To - From);
  if ((Delta & 3) != 0 || Delta < -0x8000000LL || Delta >= 0x8000000LL)
    return FALSE;
  *Instruction = 0x94000000U | ((UINT32)(Delta / 4) & 0x3FFFFFFU);
  return TRUE;
}

STATIC BOOLEAN
EncodePage(UINT64 From, UINT64 To, UINT32 *Instruction)
{
  INT64 Delta = (INT64)((To & ~0xFFFULL) - (From & ~0xFFFULL)) / 4096;
  if (Delta < -0x100000LL || Delta >= 0x100000LL)
    return FALSE;
  *Instruction = 0x90000000U | (((UINT32)Delta & 3U) << 29) | (((UINT32)Delta & 0x1FFFFCU) << 3);
  return TRUE;
}

STATIC BOOLEAN
ReadAddress(AC_KEXT_CONTEXT *Context, UINT64 Pc, UINT32 Page, UINT32 Load, UINT64 *Address)
{
  INT32 Pages = (INT32)((((Page >> 5) & 0x7FFFFU) << 2) | ((Page >> 29) & 3U));
  UINTN Index;
  if ((Page & 0x9F000000U) != 0x90000000U || (Page & 31U) == 31 ||
      (Load & 0xFFC00000U) != 0xF9400000U || ((Load >> 5) & 31U) != (Page & 31U))
    return FALSE;
  if ((Pages & 0x100000) != 0)
    Pages -= 0x200000;
  *Address = (Pc & ~0xFFFULL) + (INT64)Pages * 4096 + ((Load >> 10) & 0xFFFU) * 8;
  for (Index = 0; Index < Context->RootCount; ++Index)
  {
    AC_MACH_SEGMENT *Root = &Context->Roots[Index].Mach;
    if ((Root->InitialProtection & 5U) == 1U && *Address >= Root->VirtualAddress &&
        KextRange((UINTN)Root->FileSize, *Address - Root->VirtualAddress, 8))
      return TRUE;
  }
  return FALSE;
}

STATIC BOOLEAN
IsPrelinkRxCall(AC_KEXT_CONTEXT *Context, CONST AC_MACH_SEGMENT *Text, UINTN At, UINT64 *Mapper)
{
  STATIC CONST UINT32 Arguments[] = {0xD2800002U, 0x52800003U, 0x52800044U, 0x52800005U,
                                     0x52800066U};
  CONST UINT8        *Code = Context->Buffer + At;
  UINT64              Pc = Text->VirtualAddress + At - Text->FileOffset;
  UINT64              Address, SizeAddress;
  UINT32              Load0 = KextRead32(Code + 4), Load1 = KextRead32(Code + 12);
  UINT32              First = KextRead32(Code + 40), Second = KextRead32(Code + 44);
  UINT32              Combine = KextRead32(Code + 48), Skip = KextRead32(Code + 52);
  UINT32              Call = KextRead32(Code + 36);
  INT32               Delta = (INT32)(Call & 0x3FFFFFFU);
  if ((Load0 & 31U) != 0 || (Load1 & 31U) != 1 ||
      !ReadAddress(Context, Pc, KextRead32(Code), Load0, &Address) ||
      !ReadAddress(Context, Pc + 8, KextRead32(Code + 8), Load1, &SizeAddress) ||
      Address == SizeAddress || CompareMem(Code + 16, Arguments, sizeof(Arguments)) != 0 ||
      (Call & 0xFC000000U) != 0x94000000U)
    return FALSE;
  // The native PLK RX call is followed by the PLK data/code size test.
  // Require the second load to re-read the same code size as the call's X1.
  if ((First & 0xFFC00000U) != 0xF9400000U || (Second & ~31U) != (Load1 & ~31U) ||
      (Combine & 0xFFE0FC00U) != 0xAA000000U || ((Combine >> 5) & 31U) != (First & 31U) ||
      ((Combine >> 16) & 31U) != (Second & 31U) || (Skip & 0xFF000000U) != 0xB4000000U ||
      (Skip & 31U) != (Combine & 31U))
    return FALSE;
  if ((Delta & 0x2000000) != 0)
    Delta -= 0x4000000;
  *Mapper = Pc + 36 + (INT64)Delta * 4;
  return *Mapper >= Text->VirtualAddress &&
         KextRange((UINTN)Text->FileSize, *Mapper - Text->VirtualAddress, 4);
}

STATIC EFI_STATUS
MapExecutableTail(AC_KEXT_CONTEXT *Context)
{
  AC_MACH_SEGMENT  Text, Last;
  AC_MACH_SEGMENT *Root = NULL;
  UINTN            TextCommand, LastCommand, Index, Item, At, Hook = 0, Cave;
  UINT64           Tail, End, Mapper = 0, Target, Address;
  BOOLEAN          Needed = FALSE;
  UINT32           Call;
  UINT32 Code[] = {0xA9BF7BFDU, 0,           0,           0,           0, 0,           0xD2800002U,
                   0x52800003U, 0x52800044U, 0x52800005U, 0x52800066U, 0, 0xA8C17BFDU, 0xD65F03C0U};
  EFI_STATUS Status;
  for (Index = 0; Index < Context->Count; ++Index)
    for (Item = 0; Item < Context->Images[Index].SegmentCount; ++Item)
      if (KextName(Context->Images[Index].Segments[Item].Mach.Name, "__TEXT_EXEC"))
        Needed = TRUE;
  if (!Needed)
    return EFI_SUCCESS;
  Status = KernelSegment(Context, "__TEXT_EXEC", &Text, &TextCommand);
  if (EFI_ERROR(Status))
    return EFI_UNSUPPORTED;
  for (Index = 0; Index < Context->RootCount; ++Index)
    if (KextName(Context->Roots[Index].Mach.Name, "__TEXT_EXEC"))
      Root = &Context->Roots[Index].Mach;
  if (Root == NULL || (Root->InitialProtection & 5U) != 5U || (Text.InitialProtection & 5U) != 5U ||
      Root->VirtualAddress > Text.VirtualAddress)
    return EFI_UNSUPPORTED;
  // With kernel-first text, XNU already derives the tail from the outer segment.
  if (Root->VirtualAddress == Text.VirtualAddress)
    return EFI_SUCCESS;
  Status = KernelSegment(Context, "__LAST", &Last, &LastCommand);
  if (EFI_ERROR(Status))
    return EFI_UNSUPPORTED;
  Tail = Last.VirtualAddress + Last.VirtualSize;
  End = Root->VirtualAddress + Root->VirtualSize;
  Needed = FALSE;
  for (Index = 0; Index < Context->Count; ++Index)
    for (Item = 0; Item < Context->Images[Index].SegmentCount; ++Item)
    {
      AC_KEXT_SEGMENT *Placed = &Context->Images[Index].Segments[Item];
      if (!KextName(Placed->Mach.Name, "__TEXT_EXEC") ||
          Placed->Address + Placed->Size <= Text.VirtualAddress)
        continue;
      if (Placed->Address < Tail || Placed->Address > End || Placed->Size > End - Placed->Address)
        return EFI_UNSUPPORTED;
      Needed = TRUE;
    }
  if (!Needed)
    return EFI_SUCCESS;
  if (Tail < Text.VirtualAddress + Text.VirtualSize || Tail >= End ||
      ((Tail | End) & (AC_KEXT_PAGE - 1)) != 0 ||
      End != Context->Roots[Context->InfoRoot].Mach.VirtualAddress || End - Tail > MAX_UINT32 ||
      Text.FileSize < 56 || Text.FileSize > MAX_UINTN - 15)
    return EFI_UNSUPPORTED;
  for (At = (UINTN)Text.FileOffset; At <= Text.FileOffset + Text.FileSize - 56; At += 4)
    if (IsPrelinkRxCall(Context, &Text, At, &Target))
    {
      if (Hook != 0)
        return EFI_UNSUPPORTED;
      Hook = At + 36;
      Mapper = Target;
    }
  if (Hook == 0)
    return EFI_UNSUPPORTED;
  // Use only zero padding beyond the kernel's file-backed text and sections.
  // Extending filesize preserves every existing virtual address and page range.
  Cave = ALIGN_VALUE((UINTN)Text.FileSize, 16);
  if (!KextRange((UINTN)Text.VirtualSize, Cave, sizeof(Code)) ||
      Text.VirtualAddress + Cave < Root->VirtualAddress ||
      !KextRange((UINTN)Root->FileSize, Text.VirtualAddress + Cave - Root->VirtualAddress,
                 sizeof(Code)))
    return EFI_OUT_OF_RESOURCES;
  for (Index = 0; Index < Text.SectionCount; ++Index)
  {
    CONST UINT8 *Section = Context->Buffer + TextCommand + 72 + Index * 80;
    UINT64       Start = KextRead64(Section + 32), Length = KextRead64(Section + 40);
    if (Start < Text.VirtualAddress || Start - Text.VirtualAddress > Cave ||
        Length > Cave - (Start - Text.VirtualAddress))
      return EFI_UNSUPPORTED;
  }
  Address = Text.VirtualAddress + Cave;
  Cave += (UINTN)Text.FileOffset;
  for (Index = 0; Index < sizeof(Code); ++Index)
    if (Context->Buffer[Cave + Index] != 0)
      return EFI_OUT_OF_RESOURCES;
  for (Index = 0; Index < Context->FixupCount; ++Index)
    if (Context->Fixups[Index].Offset < Cave + sizeof(Code) &&
        Cave < Context->Fixups[Index].Offset + 8)
      return EFI_UNSUPPORTED;
  Target = Text.VirtualAddress + Hook - Text.FileOffset;
  if (!EncodeCall(Target, Address, &Call) || !EncodeCall(Address + 4, Mapper, &Code[1]) ||
      !EncodeCall(Address + 44, Mapper, &Code[11]) || !EncodePage(Address + 8, Tail, &Code[2]))
    return EFI_UNSUPPORTED;
  Code[3] = 0x91000000U | ((UINT32)(Tail & 0xFFFU) << 10);
  Code[4] = 0x52800001U | ((UINT32)((End - Tail) & 0xFFFFU) << 5);
  Code[5] = 0x72A00001U | ((UINT32)((End - Tail) >> 16) << 5);
  CopyMem(Context->Buffer + Cave, Code, sizeof(Code));
  KextWrite32(Context->Buffer + Hook, Call);
  Text.FileSize = Cave - (UINTN)Text.FileOffset + sizeof(Code);
  CopyMem(Context->Buffer + TextCommand, &Text, sizeof(Text));
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
PlaceMetadata(AC_KEXT_CONTEXT *Context, KEXT_XML *Xml)
{
  AC_KEXT_ROOT *Info = &Context->Roots[Context->InfoRoot];
  AC_KEXT_ROOT *Exec = NULL;
  UINTN         Index;
  UINTN         Reserve = ALIGN_VALUE(Xml->Used + Context->Count * 128 + 1, AC_KEXT_PAGE);
  UINTN         Shift = 0;
  UINTN         Offset;
  EFI_STATUS    Status;
  if (Reserve > Info->Mach.FileSize)
    return EFI_OUT_OF_RESOURCES;
  for (Index = 0; Index < Context->RootCount; ++Index)
    if (KextName(Context->Roots[Index].Mach.Name, "__TEXT_EXEC") &&
        Context->Roots[Index].Mach.VirtualAddress + Context->Roots[Index].Mach.VirtualSize ==
          Info->Mach.VirtualAddress &&
        Context->Roots[Index].Mach.FileOffset + Context->Roots[Index].Mach.FileSize ==
          Info->Mach.FileOffset)
      Exec = &Context->Roots[Index];
  // Reclaim only the collection's metadata mapping; no member mapping may own it.
  if (Exec != NULL)
  {
    Shift = (UINTN)((Info->Mach.FileSize - Reserve) & ~(UINT64)(AC_KEXT_PAGE - 1));
    for (Index = 0; Index < Context->UsedCount; ++Index)
      if (Context->Used[Index].Offset < Info->Mach.FileOffset + Info->Mach.FileSize &&
          Info->Mach.FileOffset < Context->Used[Index].Offset + Context->Used[Index].Size)
        return EFI_UNSUPPORTED;
    Exec->Mach.VirtualSize += Shift;
    Exec->Mach.FileSize += Shift;
  }
  Offset = (UINTN)Info->Mach.FileOffset;
  ZeroMem(Context->Buffer + Offset, (UINTN)Info->Mach.FileSize);
  Info->Mach.VirtualAddress += Shift;
  Info->Mach.FileOffset += Shift;
  Info->Mach.VirtualSize -= Shift;
  Info->Mach.FileSize -= Shift;
  Status = ReserveUsed(Context, (UINTN)Info->Mach.FileOffset, (UINTN)Info->Mach.FileSize);
  if (EFI_ERROR(Status))
    return Status;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
AppendEntry(AC_KEXT_CONTEXT *Context, AC_KEXT_IMAGE *Image)
{
  UINTN  Length = AsciiStrLen(Image->Identifier) + 1;
  UINTN  Size = ALIGN_VALUE(32 + Length, 8);
  UINT8 *At;
  UINTN  Index;
  if (Context->HeaderEnd > Context->HeaderLimit || Size > Context->HeaderLimit - Context->HeaderEnd)
    return EFI_OUT_OF_RESOURCES;
  At = Context->Buffer + Context->HeaderEnd;
  for (Index = 0; Index < Size; ++Index)
    if (At[Index] != 0)
      return EFI_COMPROMISED_DATA;
  ZeroMem(At, Size);
  KextWrite32(At, AC_MACH_LC_FILESET_ENTRY);
  KextWrite32(At + 4, (UINT32)Size);
  KextWrite64(At + 8, Image->HeaderAddress);
  KextWrite64(At + 16, Image->HeaderOffset);
  KextWrite32(At + 24, 32);
  CopyMem(At + 32, Image->Identifier, Length);
  KextWrite32(Context->Buffer + 16, KextRead32(Context->Buffer + 16) + 1);
  KextWrite32(Context->Buffer + 20, KextRead32(Context->Buffer + 20) + (UINT32)Size);
  Context->HeaderEnd += Size;
  return EFI_SUCCESS;
}

EFI_STATUS
AcKernelCollectionInject(IN CONST AC_KERNEL_COLLECTION_CONTEXT *Source,
                         IN CONST AC_KERNEL_KEXT *Kexts, IN UINTN Count,
                         OUT AC_KERNEL_COLLECTION_CONTEXT *Result, OUT UINTN *FailedIndex OPTIONAL)
{
  AC_KEXT_CONTEXT    *Context;
  EFI_STATUS          Status = EFI_INVALID_PARAMETER;
  KEXT_XML            Xml = {0};
  UINTN               Index;
  UINTN               Other;
  UINTN               Capacity;
  UINTN               Failed = MAX_UINTN;
  AC_KERNEL_LOAD_INFO Load;
  CONST CHAR8        *Name;
  UINT64              Version;
  if (FailedIndex != NULL)
    *FailedIndex = MAX_UINTN;
  if (Result == NULL || Source == Result)
    return EFI_INVALID_PARAMETER;
  ZeroMem(Result, sizeof(*Result));
  if (Source == NULL || Source->Buffer == NULL || Source->Entries == NULL ||
      (Count != 0 && Kexts == NULL) || Count > 1024)
    return EFI_INVALID_PARAMETER;
  Status = AcKernelGetLoadInfo(Source, AC_KEXT_LIMIT, &Load);
  if (EFI_ERROR(Status))
    return Status;
  if (Count == 0)
  {
    UINT8 *Copy = AllocateZeroPool(Source->BufferSize);
    if (Copy == NULL)
      return EFI_OUT_OF_RESOURCES;
    CopyMem(Copy, Source->Buffer, Source->BufferSize);
    Status = AcKernelCollectionInit(Result, Copy, Source->BufferSize, Source->BufferSize);
    if (EFI_ERROR(Status))
      FreePool(Copy);
    return Status;
  }
  Capacity = Source->BufferSize;
  for (Index = 0; Index < Count; ++Index)
  {
    if (Kexts[Index].BundlePath == NULL || Kexts[Index].BundlePath[0] == '\0' ||
        Kexts[Index].Plist == NULL || Kexts[Index].PlistSize == 0 ||
        Kexts[Index].ExecutableSize > AC_KEXT_LIMIT || Kexts[Index].PlistSize > 16U * 1024U * 1024U)
      return EFI_INVALID_PARAMETER;
    if (Capacity > AC_KEXT_LIMIT - Kexts[Index].ExecutableSize)
      return EFI_BAD_BUFFER_SIZE;
    Capacity += Kexts[Index].ExecutableSize;
  }
  if (Capacity > AC_KEXT_LIMIT - 4U * 1024U * 1024U)
    return EFI_BAD_BUFFER_SIZE;
  Capacity += 4U * 1024U * 1024U;
  Context = AllocateZeroPool(sizeof(*Context));
  if (Context == NULL)
    return EFI_OUT_OF_RESOURCES;
  Context->Source = Source;
  Context->Kexts = Kexts;
  Context->Count = Count;
  Context->Capacity = Capacity;
  Context->Size = Source->BufferSize;
  Context->Buffer = AllocateZeroPool(Capacity);
  Context->Images = AllocateZeroPool(MAX(Count, 1) * sizeof(*Context->Images));
  Xml.Capacity = 16U * 1024U * 1024U;
  Xml.Text = AllocateZeroPool(Xml.Capacity + 1);
  if (Context->Buffer == NULL || Context->Images == NULL || Xml.Text == NULL)
  {
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }
  CopyMem(Context->Buffer, Source->Buffer, Source->BufferSize);
  Status = PrepareSource(Context);
  if (EFI_ERROR(Status))
    goto Done;
  for (Index = 0; Index < Count; ++Index)
  {
    AC_KEXT_IMAGE *Image = &Context->Images[Index];
    Failed = Index;
    Status = AcPlistParse(Kexts[Index].Plist, Kexts[Index].PlistSize, &Image->Document);
    if (EFI_ERROR(Status))
      goto Done;
    Image->Info = AcPlistGetRoot(Image->Document);
    Image->Identifier = AcPlistGetString(InternalKextProperty(Image->Info, "CFBundleIdentifier"));
    if (Image->Identifier == NULL || Image->Identifier[0] == '\0' ||
        !BundleVersion(AcPlistGetString(InternalKextProperty(Image->Info, "CFBundleVersion")),
                       &Version))
    {
      Status = EFI_COMPROMISED_DATA;
      goto Done;
    }
    if (FindInfo(Context, Image->Identifier, Index) != NULL)
    {
      Status = EFI_ALREADY_STARTED;
      goto Done;
    }
    for (Other = 0; Other < Source->EntryCount; ++Other)
      if (AsciiStrCmp(Source->Entries[Other].Identifier, Image->Identifier) == 0)
      {
        Status = EFI_ALREADY_STARTED;
        goto Done;
      }
    for (Other = 0; Other < AcPlistGetCount(Image->Info); ++Other)
    {
      CONST AC_PLIST_NODE *Value;
      AcPlistGetDictionaryItem(Image->Info, Other, &Name, &Value);
      if (AsciiStrnCmp(Name, "_Prelink", 8) == 0)
      {
        Status = EFI_COMPROMISED_DATA;
        goto Done;
      }
    }
    Image->Libraries = InternalKextProperty(Image->Info, "OSBundleLibraries");
    if (Image->Libraries != NULL && AcPlistGetType(Image->Libraries) != AcPlistTypeDictionary)
    {
      Status = EFI_COMPROMISED_DATA;
      goto Done;
    }
    Status = CheckLibraries(Context, Index);
    if (EFI_ERROR(Status))
      goto Done;
    if (Kexts[Index].ExecutablePath != NULL && Kexts[Index].ExecutablePath[0] != '\0')
    {
      Image->Input = Kexts[Index].Executable;
      Image->InputSize = Kexts[Index].ExecutableSize;
      Name = AcPlistGetString(InternalKextProperty(Image->Info, "CFBundleExecutable"));
      if (Image->Input == NULL || Image->InputSize == 0 || Name == NULL || Name[0] == '\0')
      {
        Status = EFI_INVALID_PARAMETER;
        goto Done;
      }
      Status = InternalKextParse(Image);
      if (EFI_ERROR(Status))
        goto Done;
    }
    else if (Kexts[Index].Executable != NULL || Kexts[Index].ExecutableSize != 0 ||
             InternalKextProperty(Image->Info, "CFBundleExecutable") != NULL)
    {
      Status = EFI_INVALID_PARAMETER;
      goto Done;
    }
  }
  Failed = MAX_UINTN;
  {
    UINTN End = Context->HeaderEnd;
    for (Index = 0; Index < Count; ++Index)
      if (Context->Images[Index].Input != NULL)
        End += ALIGN_VALUE(32 + AsciiStrLen(Context->Images[Index].Identifier) + 1, 8);
    if (End > Context->HeaderLimit)
    {
      Status = EFI_OUT_OF_RESOURCES;
      goto Done;
    }
    Status = ReserveUsed(Context, Context->HeaderEnd, End - Context->HeaderEnd);
    if (EFI_ERROR(Status))
      goto Done;
  }
  Status = BuildMetadata(Context, &Xml);
  if (EFI_ERROR(Status))
    goto Done;
  Status = PlaceMetadata(Context, &Xml);
  if (EFI_ERROR(Status))
    goto Done;
  for (Index = 0; Index < Count; ++Index)
  {
    Failed = Index;
    if (Context->Images[Index].Input == NULL)
      continue;
    Status = InternalKextPlace(Context, &Context->Images[Index]);
    if (EFI_ERROR(Status))
      goto Done;
  }
  for (Index = 0; Index < Count; ++Index)
  {
    Failed = Index;
    if (Context->Images[Index].Input == NULL)
      continue;
    Status = InternalKextLink(Context, &Context->Images[Index], Index);
    if (!EFI_ERROR(Status))
      Status = AppendEntry(Context, &Context->Images[Index]);
    if (EFI_ERROR(Status))
      goto Done;
  }
  Failed = MAX_UINTN;
  Status = MapExecutableTail(Context);
  if (EFI_ERROR(Status))
    goto Done;
  Status = WriteRootChains(Context);
  if (EFI_ERROR(Status))
    goto Done;
  Xml.Used = 0;
  Status = BuildMetadata(Context, &Xml);
  if (EFI_ERROR(Status))
    goto Done;
  {
    AC_KEXT_ROOT *Info = &Context->Roots[Context->InfoRoot];
    if (Xml.Used + 1 > Info->Mach.FileSize)
    {
      Status = EFI_OUT_OF_RESOURCES;
      goto Done;
    }
    CopyMem(Context->Buffer + Info->Mach.FileOffset, Xml.Text, Xml.Used);
    Context->Buffer[Info->Mach.FileOffset + Xml.Used] = 0;
    for (Index = 0; Index < Info->Mach.SectionCount; ++Index)
    {
      UINT8 *Section = Context->Buffer + Info->Command + 72 + Index * 80;
      if (Index != 0)
      {
        Status = EFI_UNSUPPORTED;
        goto Done;
      }
      KextWrite64(Section + 32, Info->Mach.VirtualAddress);
      KextWrite64(Section + 40, Xml.Used + 1);
      KextWrite32(Section + 48, (UINT32)Info->Mach.FileOffset);
    }
  }
  for (Index = 0; Index < Context->RootCount; ++Index)
    CopyMem(Context->Buffer + Context->Roots[Index].Command, &Context->Roots[Index].Mach, 72);
  Status = AcKernelCollectionInit(Result, Context->Buffer, Context->Size, Context->Capacity);
  if (!EFI_ERROR(Status))
    Status = AcKernelGetLoadInfo(Result, AC_KEXT_LIMIT, &Load);
  if (EFI_ERROR(Status))
    AcKernelCollectionFree(Result);
  else
    Context->Buffer = NULL;
Done:
  if (FailedIndex != NULL && EFI_ERROR(Status))
    *FailedIndex = Failed;
  for (Index = 0; Context->Images != NULL && Index < Count; ++Index)
    AcPlistFree(Context->Images[Index].Document);
  AcPlistFree(Context->Document);
  if (Xml.Text != NULL)
    FreePool(Xml.Text);
  if (Context->Images != NULL)
    FreePool(Context->Images);
  if (Context->Fixups != NULL)
    FreePool(Context->Fixups);
  if (Context->Used != NULL)
    FreePool(Context->Used);
  if (Context->Buffer != NULL)
    FreePool(Context->Buffer);
  FreePool(Context);
  return Status;
}
