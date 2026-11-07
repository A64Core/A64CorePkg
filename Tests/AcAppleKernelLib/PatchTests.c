/** @file
  Synthetic fileset tests using public interfaces and host pool substitutes.
**/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Library/AcAppleKernelLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

STATIC UINTN OutstandingPools;
STATIC UINTN AllocationCalls;
STATIC UINTN FailAllocationAt;

VOID *EFIAPI
CopyMem(VOID *Destination, CONST VOID *Source, UINTN Length)
{
  return memmove(Destination, Source, Length);
}

VOID *EFIAPI
ZeroMem(VOID *Buffer, UINTN Length)
{
  return memset(Buffer, 0, Length);
}

INTN EFIAPI
CompareMem(CONST VOID *First, CONST VOID *Second, UINTN Length)
{
  return memcmp(First, Second, Length);
}

INTN EFIAPI
AsciiStrCmp(CONST CHAR8 *First, CONST CHAR8 *Second)
{
  return strcmp(First, Second);
}

VOID *EFIAPI
AllocateZeroPool(UINTN Size)
{
  VOID *Result;

  ++AllocationCalls;
  if ((FailAllocationAt != 0) && (AllocationCalls == FailAllocationAt))
    return NULL;

  Result = calloc(1, Size);
  if (Result != NULL)
    ++OutstandingPools;
  return Result;
}

VOID EFIAPI
FreePool(VOID *Buffer)
{
  assert((Buffer != NULL) && (OutstandingPools > 0));
  --OutstandingPools;
  free(Buffer);
}

enum
{
  PayloadSize = 0x3000,
  Capacity = PayloadSize + 0x100,
  ImageCount = 3,
  FirstHeader = 0x200,
  HeaderStride = 0x400,
  FirstCode = 0x1000,
  CodeStride = 0x400,
  CodeSize = 64,
  DataDelta = 0x100,
  Linkedit = 0x2000,
  Symbols = 0x2100,
  Strings = 0x2400,
  StringsSize = 0x300,
  SymbolCount = 13,
  RootSymtab = 32 + ImageCount * 64,
  ImageSymtabDelta = 32 + 4 * 72
};

STATIC CONST UINT8        Find[] = {0xD1, 0x62, 0xB3};
STATIC CONST UINT8        Replace[] = {0x47, 0x58, 0x69};
STATIC CONST CHAR8 *CONST Identifiers[] = {"com.apple.kernel", "org.example.Alpha",
                                           "org.example.Beta"};

typedef struct
{
  UINT8                        Storage[Capacity + 1];
  UINT8                       *Buffer;
  AC_KERNEL_COLLECTION_CONTEXT Context;
} FIXTURE;

STATIC VOID
Put32(UINT8 *Buffer, UINTN Offset, UINT32 Value)
{
  CopyMem(Buffer + Offset, &Value, sizeof(Value));
}

STATIC VOID
Put64(UINT8 *Buffer, UINTN Offset, UINT64 Value)
{
  CopyMem(Buffer + Offset, &Value, sizeof(Value));
}

STATIC UINTN
HeaderOffset(UINT32 Image)
{
  return FirstHeader + Image * HeaderStride;
}

STATIC UINTN
CodeOffset(UINT32 Image)
{
  return FirstCode + Image * CodeStride;
}

STATIC UINT64
ImageAddress(UINT32 Image)
{
  return 0x100000000ULL + Image * 0x10000ULL;
}

STATIC VOID
WriteHeader(UINT8 *Buffer, UINTN Offset, UINT32 Type, UINT32 Commands, UINT32 CommandsSize)
{
  Put32(Buffer, Offset, 0xFEEDFACF);
  Put32(Buffer, Offset + 4, 0x0100000C);
  Put32(Buffer, Offset + 8, 2);
  Put32(Buffer, Offset + 12, Type);
  Put32(Buffer, Offset + 16, Commands);
  Put32(Buffer, Offset + 20, CommandsSize);
}

STATIC VOID
WriteSegment(UINT8 *Buffer, UINTN Offset, CONST CHAR8 *Name, UINT64 Address, UINT64 VirtualSize,
             UINT64 FileOffset, UINT64 FileSize)
{
  Put32(Buffer, Offset, 0x19);
  Put32(Buffer, Offset + 4, 72);
  CopyMem(Buffer + Offset + 8, Name, strlen(Name));
  Put64(Buffer, Offset + 24, Address);
  Put64(Buffer, Offset + 32, VirtualSize);
  Put64(Buffer, Offset + 40, FileOffset);
  Put64(Buffer, Offset + 48, FileSize);
}

STATIC VOID
WriteSymtab(UINT8 *Buffer, UINTN Offset)
{
  Put32(Buffer, Offset, 2);
  Put32(Buffer, Offset + 4, 24);
  Put32(Buffer, Offset + 8, Symbols);
  Put32(Buffer, Offset + 12, SymbolCount);
  Put32(Buffer, Offset + 16, Strings);
  Put32(Buffer, Offset + 20, StringsSize);
}

STATIC VOID
BuildCollection(UINT8 *Buffer)
{
  STATIC CONST CHAR8  Names[] = "\0_entry\0_data\0_bss\0_header\0";
  STATIC CONST CHAR8  Banner[] = "Darwin Kernel Version 17.42.3:";
  STATIC CONST UINT32 NameOffsets[] = {1, 8, 14, 19};
  UINT32              Image;
  UINT32              Index;
  UINTN               Header;
  UINTN               Code;
  UINTN               Symbol;
  UINT64              Address;

  ZeroMem(Buffer, PayloadSize);
  WriteHeader(Buffer, 0, 0xC, ImageCount + 1, ImageCount * 64 + 24);
  WriteSymtab(Buffer, RootSymtab);
  CopyMem(Buffer + Strings, Names, sizeof(Names));
  // The first record is an undefined _entry; defined records follow it.
  Put32(Buffer, Symbols, 1);
  for (Image = 0; Image < ImageCount; ++Image)
  {
    Header = HeaderOffset(Image);
    Code = CodeOffset(Image);
    Address = ImageAddress(Image);
    Put32(Buffer, 32 + Image * 64, 0x80000035);
    Put32(Buffer, 36 + Image * 64, 64);
    Put64(Buffer, 40 + Image * 64, Address);
    Put64(Buffer, 48 + Image * 64, Header);
    Put32(Buffer, 56 + Image * 64, 32);
    CopyMem(Buffer + 64 + Image * 64, Identifiers[Image], strlen(Identifiers[Image]) + 1);
    WriteHeader(Buffer, Header, Image == 0 ? 2 : 0xB, 5, 4 * 72 + 24);
    WriteSegment(Buffer, Header + 32, "__TEXT", Address, 0x180, Header, 0x180);
    WriteSegment(Buffer, Header + 32 + 72, "__TEXT_EXEC", Address + 0x1000, CodeSize, Code,
                 CodeSize);
    WriteSegment(Buffer, Header + 32 + 2 * 72, "__DATA", Address + 0x2000, 128, Code + DataDelta,
                 64);
    WriteSegment(Buffer, Header + 32 + 3 * 72, "__LINKEDIT", 0x900000000ULL, 0x800, Linkedit,
                 0x800);
    WriteSymtab(Buffer, Header + ImageSymtabDelta);
    for (Index = 0; Index < 3; ++Index)
      CopyMem(Buffer + Code + Index * 8, Find, sizeof(Find));
    CopyMem(Buffer + Code + DataDelta, Banner, sizeof(Banner));
    for (Index = 0; Index < 4; ++Index)
    {
      Symbol = Symbols + (1 + Image * 4 + Index) * 16;
      Put32(Buffer, Symbol, NameOffsets[Index]);
      Buffer[Symbol + 4] = 0x0F;
      Buffer[Symbol + 5] = 1;
      Put64(Buffer, Symbol + 8,
            Index == 0   ? Address + 0x1000
            : Index == 1 ? Address + 0x2000 + 4
            : Index == 2 ? Address + 0x2000 + 72
                         : Address);
    }
  }
}

STATIC VOID
Initialize(FIXTURE *Fixture)
{
  ZeroMem(Fixture, sizeof(*Fixture));
  Fixture->Buffer = Fixture->Storage + 1;
  BuildCollection(Fixture->Buffer);
  memset(Fixture->Buffer + PayloadSize, 0xA5, Capacity - PayloadSize);
  assert(AcKernelCollectionInit(&Fixture->Context, Fixture->Buffer, PayloadSize, Capacity) ==
         EFI_SUCCESS);
}

STATIC VOID
Finish(FIXTURE *Fixture)
{
  UINTN Index;

  AcKernelCollectionFree(&Fixture->Context);
  for (Index = PayloadSize; Index < Capacity; ++Index)
    assert(Fixture->Buffer[Index] == 0xA5);
  assert(OutstandingPools == 0);
}

STATIC AC_KERNEL_PATCH
DefaultPatch(VOID)
{
  AC_KERNEL_PATCH Patch = {0};
  Patch.Find = Find;
  Patch.Replace = Replace;
  Patch.Size = sizeof(Find);
  return Patch;
}

STATIC VOID
ExpectPatch(FIXTURE *Fixture, CONST CHAR8 *Target, CONST AC_KERNEL_PATCH *Patch,
            EFI_STATUS ExpectedStatus, UINT32 ExpectedCount)
{
  EFI_STATUS Status;
  UINT32     Count = MAX_UINT32;
  Status = AcKernelCollectionApplyPatch(&Fixture->Context, Target, Patch, &Count);
  if ((Status != ExpectedStatus) || (Count != ExpectedCount))
    fprintf(stderr, "Patch %s: status %llx count %u, expected %llx count %u\n", Target,
            (unsigned long long)Status, Count, (unsigned long long)ExpectedStatus, ExpectedCount);
  assert(Status == ExpectedStatus);
  assert(Count == ExpectedCount);
}

STATIC VOID
TestSelectionAndOrder(VOID)
{
  FIXTURE                Fixture;
  AC_KERNEL_PATCH        Patch = DefaultPatch();
  UINT32                 Version;
  AC_KERNEL_PAYLOAD_TYPE Type;

  Initialize(&Fixture);
  assert(AcKernelIdentifyPayload(Fixture.Buffer, PayloadSize, &Type) == EFI_SUCCESS);
  assert(Type == AcKernelPayloadCollection);
  assert(AcKernelReadDarwinVersion(&Fixture.Context, &Version) == EFI_SUCCESS && Version == 174203);
  Patch.Count = 1;
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_SUCCESS, 1);
  assert(CompareMem(Fixture.Buffer + FirstCode, Replace, sizeof(Find)) == 0);
  assert(CompareMem(Fixture.Buffer + CodeOffset(1), Find, sizeof(Find)) == 0);
  ExpectPatch(&Fixture, Identifiers[1], &Patch, EFI_SUCCESS, 1);
  assert(CompareMem(Fixture.Buffer + CodeOffset(1), Replace, sizeof(Find)) == 0);
  assert(CompareMem(Fixture.Buffer + CodeOffset(2), Find, sizeof(Find)) == 0);
  Patch.Find = Replace;
  Patch.Replace = Find;
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_SUCCESS, 1);
  assert(CompareMem(Fixture.Buffer + FirstCode, Find, sizeof(Find)) == 0);
  ExpectPatch(&Fixture, "org.example.Missing", &Patch, EFI_NOT_FOUND, 0);
  Finish(&Fixture);
}

STATIC VOID
TestCountSkipAndLimit(VOID)
{
  FIXTURE         Fixture;
  AC_KERNEL_PATCH Patch;
  UINT32          Case;

  for (Case = 0; Case < 5; ++Case)
  {
    Initialize(&Fixture);
    Patch = DefaultPatch();
    Patch.Count = Case == 0 ? 2 : Case == 1 ? 1 : Case == 3 ? 9 : 0;
    Patch.Skip = Case == 1 ? 1 : Case == 2 ? 9 : 0;
    ExpectPatch(&Fixture, "kernel", &Patch, Case == 2 ? EFI_NOT_FOUND : EFI_SUCCESS,
                Case == 0   ? 2
                : Case == 1 ? 1
                : Case == 2 ? 0
                            : 3);
    if (Case == 1)
    {
      assert(CompareMem(Fixture.Buffer + FirstCode, Find, sizeof(Find)) == 0);
      assert(CompareMem(Fixture.Buffer + FirstCode + 8, Replace, sizeof(Find)) == 0);
    }
    Finish(&Fixture);
  }

  Initialize(&Fixture);
  Patch = DefaultPatch();
  Patch.Limit = FirstCode - FirstHeader + sizeof(Find) - 1;
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_NOT_FOUND, 0);
  ++Patch.Limit;
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_SUCCESS, 1);
  Patch.Base = "_entry";
  Patch.Limit = 2;
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_NOT_FOUND, 0);
  Patch.Find = Replace;
  Patch.Limit = 3;
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_SUCCESS, 1);
  Finish(&Fixture);
}

STATIC VOID
TestMasksAndNonoverlap(VOID)
{
  FIXTURE         Fixture;
  AC_KERNEL_PATCH Patch = {0};
  CONST UINT8     Pattern[] = {0xA0, 0x50};
  CONST UINT8     BadPattern[] = {0xA7, 0x50};
  CONST UINT8     Mask[] = {0xF0, 0xF0};
  CONST UINT8     Replacement[] = {0x0F, 0xF0};
  CONST UINT8     ReplaceMask[] = {0x0F, 0xF0};
  CONST UINT8     ZeroMask[] = {0, 0};
  CONST UINT8     Repeated[] = {0xAA, 0xAA};
  CONST UINT8     Changed[] = {0xBB, 0xBB};

  Initialize(&Fixture);
  Fixture.Buffer[FirstCode] = 0xA7;
  Fixture.Buffer[FirstCode + 1] = 0x5C;
  Patch.Base = "_entry";
  Patch.Limit = 2;
  Patch.Find = BadPattern;
  Patch.Replace = Replacement;
  Patch.Mask = Mask;
  Patch.ReplaceMask = ReplaceMask;
  Patch.Size = 2;
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_NOT_FOUND, 0);
  Patch.Find = Pattern;
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_SUCCESS, 1);
  assert(Fixture.Buffer[FirstCode] == 0xAF && Fixture.Buffer[FirstCode + 1] == 0xFC);
  Patch.Find = Replacement;
  Patch.Mask = ReplaceMask;
  Patch.ReplaceMask = ZeroMask;
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_SUCCESS, 1);
  assert(Fixture.Buffer[FirstCode] == 0xAF && Fixture.Buffer[FirstCode + 1] == 0xFC);
  Finish(&Fixture);

  Initialize(&Fixture);
  memset(Fixture.Buffer + FirstCode, 0xAA, 5);
  Patch = DefaultPatch();
  Patch.Find = Repeated;
  Patch.Replace = Changed;
  Patch.Size = 2;
  Patch.Skip = 1;
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_SUCCESS, 1);
  assert(Fixture.Buffer[FirstCode] == 0xAA && Fixture.Buffer[FirstCode + 1] == 0xAA);
  assert(Fixture.Buffer[FirstCode + 2] == 0xBB && Fixture.Buffer[FirstCode + 3] == 0xBB);
  assert(Fixture.Buffer[FirstCode + 4] == 0xAA);
  Finish(&Fixture);
}

STATIC VOID
TestBaseSymbols(VOID)
{
  FIXTURE         Fixture;
  AC_KERNEL_PATCH Patch = DefaultPatch();
  UINT32          Image;

  for (Image = 0; Image < ImageCount; ++Image)
  {
    Initialize(&Fixture);
    Patch.Base = "_entry";
    Patch.Find = NULL;
    Patch.Count = 9;
    Patch.Skip = MAX_UINT32;
    Patch.Limit = 1;
    Patch.Mask = Find;
    Patch.ReplaceMask = Find;
    ExpectPatch(&Fixture, Identifiers[Image], &Patch, EFI_SUCCESS, 1);
    assert(CompareMem(Fixture.Buffer + CodeOffset(Image), Replace, sizeof(Replace)) == 0);
    assert(CompareMem(Fixture.Buffer + CodeOffset(Image) + 8, Find, sizeof(Find)) == 0);
    Finish(&Fixture);
  }

  Initialize(&Fixture);
  Patch = DefaultPatch();
  Patch.Base = "_missing";
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_NOT_FOUND, 0);
  Patch.Base = "_bss";
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_NOT_FOUND, 0);
  Patch.Base = "_entry";
  Patch.Count = 1;
  // An LC_UUID replaces the local SYMTAB; the collection SYMTAB is the fallback.
  Put32(Fixture.Buffer, FirstHeader + ImageSymtabDelta, 0x1B);
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_SUCCESS, 1);
  Finish(&Fixture);

  Initialize(&Fixture);
  Put32(Fixture.Buffer, FirstHeader + ImageSymtabDelta, 0x1B);
  Put32(Fixture.Buffer, RootSymtab, 0x1B);
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_NOT_FOUND, 0);
  Patch.Base = NULL;
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_SUCCESS, 1);
  Patch.Base = "_entry";
  Finish(&Fixture);

  Initialize(&Fixture);
  Patch.Find = NULL;
  Fixture.Buffer[Symbols + 16 + 4] = 3; // N_ABS | N_EXT.
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_SUCCESS, 1);
  Finish(&Fixture);

  Initialize(&Fixture);
  Fixture.Buffer[Symbols + 16 + 4] = 0x0B; // N_INDR | N_EXT.
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_UNSUPPORTED, 0);
  Finish(&Fixture);
}

STATIC VOID
TestRanges(VOID)
{
  FIXTURE         Fixture;
  AC_KERNEL_PATCH Patch = DefaultPatch();
  CONST UINT8     Across[] = {0x91, 0xA2, 0xB3, 0xC4};
  CONST UINT8     AcrossReplace[] = {1, 2, 3, 4};

  Initialize(&Fixture);
  ZeroMem(Fixture.Buffer + FirstCode, CodeSize);
  CopyMem(Fixture.Buffer + FirstCode + CodeSize + 4, Find, sizeof(Find));
  CopyMem(Fixture.Buffer + Linkedit + 0x600, Find, sizeof(Find));
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_NOT_FOUND, 0);
  assert(CompareMem(Fixture.Buffer + CodeOffset(1), Find, sizeof(Find)) == 0);
  Patch.Base = "_entry";
  Patch.Find = NULL;
  Put64(Fixture.Buffer, Symbols + 16 + 8, ImageAddress(0) + 0x1000 + CodeSize - 2);
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_UNSUPPORTED, 0);
  Put64(Fixture.Buffer, Symbols + 16 + 8, 0x900000000ULL + 0x600);
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_UNSUPPORTED, 0);
  Finish(&Fixture);

  Initialize(&Fixture);
  Patch = DefaultPatch();
  Patch.Find = Across;
  Patch.Replace = AcrossReplace;
  Patch.Size = sizeof(Across);
  CopyMem(Fixture.Buffer + FirstCode + CodeSize - 2, Across, 2);
  CopyMem(Fixture.Buffer + FirstCode + DataDelta, Across + 2, 2);
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_NOT_FOUND, 0);
  // Adjacent file-backed segments form one searchable range.
  Put64(Fixture.Buffer, FirstHeader + 32 + 2 * 72 + 40, FirstCode + CodeSize);
  CopyMem(Fixture.Buffer + FirstCode + CodeSize, Across + 2, 2);
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_SUCCESS, 1);
  assert(CompareMem(Fixture.Buffer + FirstCode + CodeSize - 2, AcrossReplace, 4) == 0);
  Finish(&Fixture);

  Initialize(&Fixture);
  CopyMem(Fixture.Buffer + FirstCode + DataDelta, Find, sizeof(Find));
  Patch = DefaultPatch();
  Patch.Base = "_entry";
  Patch.Skip = 3;
  Patch.Count = 1;
  Patch.Limit = DataDelta + sizeof(Find) - 1;
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_NOT_FOUND, 0);
  ++Patch.Limit;
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_SUCCESS, 1);
  assert(CompareMem(Fixture.Buffer + FirstCode + DataDelta, Replace, sizeof(Replace)) == 0);
  Finish(&Fixture);

  Initialize(&Fixture);
  // A large target segment surrounds a peer segment. Only the owned prefix
  // and suffix are searchable; the peer's header and code remain protected.
  Put64(Fixture.Buffer, FirstHeader + 32 + 72 + 32, 0x500);
  Put64(Fixture.Buffer, FirstHeader + 32 + 72 + 48, 0x500);
  CopyMem(Fixture.Buffer + CodeOffset(1) - 8, Find, sizeof(Find));
  CopyMem(Fixture.Buffer + CodeOffset(1) + CodeSize, Find, sizeof(Find));
  Patch = DefaultPatch();
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_SUCCESS, 5);
  assert(CompareMem(Fixture.Buffer + CodeOffset(1), Find, sizeof(Find)) == 0);
  assert(CompareMem(Fixture.Buffer + CodeOffset(1) - 8, Replace, sizeof(Replace)) == 0);
  assert(CompareMem(Fixture.Buffer + CodeOffset(1) + CodeSize, Replace, sizeof(Replace)) == 0);
  Finish(&Fixture);
}

STATIC VOID
TestMalformedSymbols(VOID)
{
  FIXTURE         Fixture;
  AC_KERNEL_PATCH Patch = DefaultPatch();
  UINT32          Case;

  Patch.Base = "_entry";
  for (Case = 0; Case < 8; ++Case)
  {
    Initialize(&Fixture);
    switch (Case)
    {
    case 0:
      Put32(Fixture.Buffer, FirstHeader + ImageSymtabDelta + 8, MAX_UINT32);
      break;
    case 1:
      Put32(Fixture.Buffer, FirstHeader + ImageSymtabDelta + 12, MAX_UINT32);
      break;
    case 2:
      Put32(Fixture.Buffer, FirstHeader + ImageSymtabDelta + 16, MAX_UINT32);
      break;
    case 3:
      Put32(Fixture.Buffer, FirstHeader + ImageSymtabDelta + 20, MAX_UINT32);
      break;
    case 4:
      Put32(Fixture.Buffer, Symbols + 16, StringsSize);
      break;
    case 5:
      memset(Fixture.Buffer + Strings, 'x', StringsSize);
      break;
    case 6:
      // Two definitions map the same name to different target addresses.
      Put32(Fixture.Buffer, Symbols + 2 * 16, 1);
      Put64(Fixture.Buffer, Symbols + 2 * 16 + 8, ImageAddress(0) + 0x1000 + 8);
      break;
    default:
      // One virtual address has conflicting file mappings.
      Put64(Fixture.Buffer, FirstHeader + 32 + 2 * 72 + 24, ImageAddress(0) + 0x1000);
      break;
    }
    ExpectPatch(&Fixture, "kernel", &Patch, EFI_COMPROMISED_DATA, 0);
    assert(CompareMem(Fixture.Buffer + FirstCode, Find, sizeof(Find)) == 0);
    Finish(&Fixture);
  }
}

STATIC VOID
TestMetadataAndArguments(VOID)
{
  FIXTURE                     Fixture;
  AC_KERNEL_PATCH             Patch = DefaultPatch();
  UINT8                       Header[32];
  UINT32                      Count;
  UINT32                      Version;
  UINT32                      Case;
  UINT32                      Image;
  AC_KERNEL_COLLECTION_ENTRY *Entries;

  Initialize(&Fixture);
  Entries = Fixture.Context.Entries;
  CopyMem(Header, Fixture.Buffer + FirstHeader, sizeof(Header));
  Put32(Header, 24, 1);
  Patch.Base = "_header";
  Patch.Find = NULL;
  Patch.Replace = Header;
  Patch.Size = sizeof(Header);
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_SUCCESS, 1);
  assert(Fixture.Context.Entries == Entries);
  assert(AcKernelReadDarwinVersion(&Fixture.Context, &Version) == EFI_SUCCESS && Version == 174203);
  Patch = DefaultPatch();
  Patch.Count = 1;
  assert(AcKernelCollectionApplyPatch(&Fixture.Context, "kernel", &Patch, NULL) == EFI_SUCCESS);
  assert(Fixture.Context.Entries == Entries);
  Patch.Find = NULL;
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_INVALID_PARAMETER, 0);
  Patch.Base = "";
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_INVALID_PARAMETER, 0);
  Patch = DefaultPatch();
  Patch.Size = 0;
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_INVALID_PARAMETER, 0);
  Patch.Size = sizeof(Find);
  Patch.Replace = NULL;
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_INVALID_PARAMETER, 0);
  Count = 1;
  assert(AcKernelCollectionApplyPatch(NULL, "kernel", &Patch, &Count) == EFI_INVALID_PARAMETER &&
         Count == 0);
  assert(AcKernelCollectionApplyPatch(&Fixture.Context, NULL, &Patch, &Count) ==
         EFI_INVALID_PARAMETER);
  assert(AcKernelCollectionApplyPatch(&Fixture.Context, "", &Patch, &Count) ==
         EFI_INVALID_PARAMETER);
  assert(AcKernelCollectionApplyPatch(&Fixture.Context, "kernel", NULL, &Count) ==
         EFI_INVALID_PARAMETER);
  Finish(&Fixture);

  for (Case = 0; Case < 3; ++Case)
  {
    Initialize(&Fixture);
    Image = Case == 2 ? 1 : 0;
    CopyMem(Header, Fixture.Buffer + HeaderOffset(Image), sizeof(Header));
    if (Case == 0)
      Header[0] = 0;
    else
      Put32(Header, 12, Case == 1 ? 0xB : 0xC);
    Patch = DefaultPatch();
    Patch.Base = "_header";
    Patch.Find = NULL;
    Patch.Replace = Header;
    Patch.Size = sizeof(Header);
    ExpectPatch(&Fixture, Identifiers[Image], &Patch, EFI_COMPROMISED_DATA, 1);
    assert(CompareMem(Fixture.Buffer + HeaderOffset(Image), Header, sizeof(Header)) == 0);
    assert(Fixture.Context.Buffer == NULL);
    Finish(&Fixture);
  }
}

STATIC VOID
TestAllocationFailures(VOID)
{
  FIXTURE         Fixture;
  AC_KERNEL_PATCH Patch = DefaultPatch();
  UINTN           Failure;
  UINTN           AllocationCount;
  UINT32          Count;
  UINT32          Version;
  EFI_STATUS      Status;
  UINT8           Expected[PayloadSize];

  Patch.Count = 1;
  Initialize(&Fixture);
  AllocationCalls = 0;
  ExpectPatch(&Fixture, "kernel", &Patch, EFI_SUCCESS, 1);
  AllocationCount = AllocationCalls;
  Finish(&Fixture);

  for (Failure = 1; Failure <= AllocationCount; ++Failure)
  {
    Initialize(&Fixture);
    CopyMem(Expected, Fixture.Buffer, sizeof(Expected));
    AllocationCalls = 0;
    FailAllocationAt = Failure;
    Count = MAX_UINT32;
    Status = AcKernelCollectionApplyPatch(&Fixture.Context, "kernel", &Patch, &Count);
    FailAllocationAt = 0;
    assert(Status == EFI_OUT_OF_RESOURCES && Count <= Patch.Count);
    if (Count != 0)
      CopyMem(Expected + FirstCode, Replace, sizeof(Replace));
    assert(CompareMem(Fixture.Buffer, Expected, sizeof(Expected)) == 0);

    assert(Fixture.Context.Buffer == Fixture.Buffer);
    assert(AcKernelReadDarwinVersion(&Fixture.Context, &Version) == EFI_SUCCESS &&
           Version == 174203);
    if (Count == 0)
      ExpectPatch(&Fixture, "kernel", &Patch, EFI_SUCCESS, 1);
    else
    {
      Patch.Find = Replace;
      Patch.Replace = Find;
      ExpectPatch(&Fixture, "kernel", &Patch, EFI_SUCCESS, 1);
      Patch = DefaultPatch();
      Patch.Count = 1;
    }
    Finish(&Fixture);
  }
}

STATIC VOID
TestTruncationAndMutations(VOID)
{
  UINT8                        Original[PayloadSize];
  UINT8                        Mutated[PayloadSize];
  UINT8                       *Allocation;
  UINT8                       *Buffer;
  AC_KERNEL_COLLECTION_CONTEXT Context;
  AC_KERNEL_PATCH              Patch = DefaultPatch();
  AC_KERNEL_PAYLOAD_TYPE       Type;
  UINT32                       Count;
  UINT32                       Random;
  UINT32                       Iteration;
  UINT32                       Mutation;
  UINTN                        Length;
  UINTN                        Offset;
  EFI_STATUS                   Status;

  BuildCollection(Original);
  Patch.Count = 1;
  for (Length = 0; Length <= PayloadSize; ++Length)
  {
    Allocation = malloc(Length + 1);
    assert(Allocation != NULL);
    Buffer = Allocation + 1;
    CopyMem(Buffer, Original, Length);
    AcKernelIdentifyPayload(Buffer, Length, &Type);
    Status = AcKernelCollectionInit(&Context, Buffer, Length, Length);
    if (!EFI_ERROR(Status))
    {
      AcKernelCollectionApplyPatch(&Context, "kernel", &Patch, &Count);
      AcKernelCollectionFree(&Context);
    }
    assert(OutstandingPools == 0);
    free(Allocation);
  }

  Random = 0x76543210;
  for (Iteration = 0; Iteration < 5000; ++Iteration)
  {
    CopyMem(Mutated, Original, sizeof(Mutated));
    for (Mutation = 0; Mutation < 4; ++Mutation)
    {
      Random = Random * 1664525U + 1013904223U;
      Offset = (Random >> 8) % PayloadSize;
      Random = Random * 1664525U + 1013904223U;
      Mutated[Offset] = (UINT8)(Random >> 24);
    }
    Status = AcKernelCollectionInit(&Context, Mutated, sizeof(Mutated), sizeof(Mutated));
    if (!EFI_ERROR(Status))
    {
      Patch.Base = (Iteration & 1) != 0 ? "_entry" : NULL;
      AcKernelCollectionApplyPatch(&Context, "kernel", &Patch, &Count);
      AcKernelCollectionFree(&Context);
    }
    assert(OutstandingPools == 0);
  }
}

int
main(VOID)
{
  TestSelectionAndOrder();
  TestCountSkipAndLimit();
  TestMasksAndNonoverlap();
  TestBaseSymbols();
  TestRanges();
  TestMalformedSymbols();
  TestMetadataAndArguments();
  TestAllocationFailures();
  TestTruncationAndMutations();
  puts("Kernel patch tests passed (ASan/UBSan, 12289 truncations, 5000 mutations).");
  return 0;
}
