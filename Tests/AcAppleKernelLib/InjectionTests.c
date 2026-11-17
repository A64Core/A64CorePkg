/** @file
  Synthetic multi-kext Boot KC linking, preservation and failure tests.
**/
#if defined(__linux__) && defined(__aarch64__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__aarch64__)
#include <sys/mman.h>
#endif
#include "../../Library/AcAppleKernelLib/AppleKernelInternal.h"

STATIC UINTN Pools;
STATIC UINTN Calls;
STATIC UINTN FailAt;

VOID *EFIAPI
CopyMem(VOID *To, CONST VOID *From, UINTN Size)
{
  return memmove(To, From, Size);
}
VOID *EFIAPI
ZeroMem(VOID *To, UINTN Size)
{
  return memset(To, 0, Size);
}
INTN EFIAPI
CompareMem(CONST VOID *A, CONST VOID *B, UINTN Size)
{
  return memcmp(A, B, Size);
}
UINTN EFIAPI
AsciiStrLen(CONST CHAR8 *Text)
{
  return strlen(Text);
}
INTN EFIAPI
AsciiStrCmp(CONST CHAR8 *A, CONST CHAR8 *B)
{
  return strcmp(A, B);
}
INTN EFIAPI
AsciiStrnCmp(CONST CHAR8 *A, CONST CHAR8 *B, UINTN Size)
{
  return strncmp(A, B, Size);
}
VOID *EFIAPI
AllocateZeroPool(UINTN Size)
{
  VOID *Buffer;
  if (++Calls == FailAt)
    return NULL;
  Buffer = calloc(1, Size);
  if (Buffer != NULL)
    ++Pools;
  return Buffer;
}
VOID EFIAPI
FreePool(VOID *Buffer)
{
  assert(Buffer != NULL && Pools != 0);
  --Pools;
  free(Buffer);
}

#define BASE 0xFFFFFE0010000000ULL
#define ROOT_SIZE 0x24000U
#define KEXT_SIZE 0x10000U
STATIC CONST CHAR8 Prelink[] =
  "<plist><dict><key>Other</key><array><integer>-1</integer><string>A&amp;B</string></array>"
  "<key>_PrelinkInfoDictionary</key><array>"
  "<dict><key>CFBundleIdentifier</key><string>com.apple.kernel</string>"
  "<key>CFBundleVersion</key><string>9.0</string>"
  "<key>OSBundleCompatibleVersion</key><string>1.0</string></dict>"
  "<dict><key>CFBundleIdentifier</key><string>com.apple.kpi.iokit</string>"
  "<key>CFBundleVersion</key><string>9.0</string>"
  "<key>OSBundleCompatibleVersion</key><string>1.0</string></dict>"
  "</array></dict></plist>";
STATIC CONST CHAR8 AlphaInfo[] =
  "<plist><dict><key>CFBundleIdentifier</key><string>org.example.Alpha</string>"
  "<key>CFBundleExecutable</key><string>Alpha</string><key>CFBundleVersion</key><string>1.0</"
  "string>"
  "<key>OSBundleCompatibleVersion</key><string>1.0</string>"
  "<key>OSBundleLibraries</key><dict><key>com.apple.kpi.iokit</key><string>1.0</string></dict>"
  "<key>IOKitPersonalities</key><dict><key>A&amp;B</key><dict><key>IOClass</key>"
  "<string>Example</string></dict></dict></dict></plist>";
STATIC CONST CHAR8 BetaInfo[] =
  "<plist><dict><key>CFBundleIdentifier</key><string>org.example.Beta</string>"
  "<key>CFBundleExecutable</key><string>Beta</string><key>CFBundleVersion</key><string>1.0</string>"
  "<key>OSBundleLibraries</key><dict><key>org.example.Alpha</key><string>1.0</string></dict>"
  "</dict></plist>";
STATIC CONST CHAR8 CodelessInfo[] =
  "<plist><dict><key>CFBundleIdentifier</key><string>org.example.Personality</string>"
  "<key>CFBundleVersion</key><string>1.0</string><key>IOKitPersonalities</key><dict/>"
  "</dict></plist>";

STATIC VOID
Header(UINT8 *Buffer, UINTN At, UINT32 Type, UINT32 Commands, UINT32 Bytes)
{
  AC_MACH_HEADER Header = {AC_MACH_MAGIC_64, AC_MACH_CPU_ARM64, 2, Type, Commands, Bytes, 0, 0};
  CopyMem(Buffer + At, &Header, 32);
}

STATIC VOID
Segment(UINT8 *Buffer, UINTN At, CONST CHAR8 *Name, UINT64 Address, UINTN File, UINTN Size,
        UINT32 Protection, UINT32 Sections)
{
  AC_MACH_SEGMENT Segment = {0};
  Segment.Command.Type = 0x19;
  Segment.Command.Size = 72 + Sections * 80;
  CopyMem(Segment.Name, Name, strlen(Name));
  Segment.VirtualAddress = Address;
  Segment.VirtualSize = Size;
  Segment.FileOffset = File;
  Segment.FileSize = Size;
  Segment.MaximumProtection = Segment.InitialProtection = Protection;
  Segment.SectionCount = Sections;
  CopyMem(Buffer + At, &Segment, 72);
}

STATIC VOID
Section(UINT8 *Buffer, UINTN At, CONST CHAR8 *Name, CONST CHAR8 *SegmentName, UINT64 Address,
        UINTN Offset, UINTN Size)
{
  AC_KEXT_SECTION Section = {0};
  CopyMem(Section.Name, Name, strlen(Name));
  CopyMem(Section.Segment, SegmentName, strlen(SegmentName));
  Section.Address = Address;
  Section.Offset = (UINT32)Offset;
  Section.Size = Size;
  Section.Alignment = 2;
  CopyMem(Buffer + At, &Section, 80);
}

STATIC VOID
Symbols(UINT8 *Buffer, UINTN Command, UINTN At, CONST CHAR8 *Code, UINT64 Address, UINT64 Kmod)
{
  AC_MACH_SYMTAB Table = {{2, 24}, (UINT32)At, Kmod == 0 ? 1 : 2, (UINT32)At + 32, 0};
  AC_MACH_SYMBOL Symbol = {1, 0xF, 1, 0, Address};
  UINTN          Length = strlen(Code);
  Table.StringsSize = (UINT32)Length + 2 + (Kmod == 0 ? 0 : 11);
  CopyMem(Buffer + At, &Symbol, 16);
  CopyMem(Buffer + At + 33, Code, Length + 1);
  if (Kmod != 0)
  {
    Symbol.StringIndex = (UINT32)Length + 2;
    Symbol.Section = 2;
    Symbol.Value = Kmod;
    CopyMem(Buffer + At + 16, &Symbol, 16);
    CopyMem(Buffer + At + 32 + Length + 2, "_kmod_info", 11);
  }
  CopyMem(Buffer + Command, &Table, 24);
}

STATIC VOID
BuildRoot(UINT8 *Buffer)
{
  UINTN                 At = 32;
  AC_MACH_FILESET_ENTRY Entry = {{0x80000035U, 56}, BASE + 0x4000, 0x4000, 32, 0};
  AC_MACH_UNIXTHREAD    Thread = {{5, 288}, 6, 68, {0}};
  ZeroMem(Buffer, ROOT_SIZE);
  Header(Buffer, 0, 0xC, 8, 5 * 72 + 56 + 288 + 16);
  Segment(Buffer, At, "__TEXT", BASE, 0, 0x8000, 1, 0);
  At += 72;
  Segment(Buffer, At, "__TEXT_EXEC", BASE + 0x8000, 0x8000, 0x4000, 5, 0);
  At += 72;
  Segment(Buffer, At, "__PRELINK_INFO", BASE + 0xC000, 0xC000, 0xC000, 1, 0);
  At += 72;
  Segment(Buffer, At, "__DATA", BASE + 0x18000, 0x18000, 0x8000, 3, 0);
  At += 72;
  Segment(Buffer, At, "__LINKEDIT", BASE + 0x20000, 0x20000, 0x4000, 1, 0);
  At += 72;
  CopyMem(Buffer + At, &Entry, 32);
  CopyMem(Buffer + At + 32, "com.apple.kernel", 17);
  At += 56;
  Thread.State.Pc = BASE + 0x8000;
  CopyMem(Buffer + At, &Thread, 288);
  At += 288;
  KextWrite32(Buffer + At, 0x80000034U);
  KextWrite32(Buffer + At + 4, 16);
  KextWrite32(Buffer + At + 8, 0x20800);
  KextWrite32(Buffer + At + 12, 78);
  Header(Buffer, 0x4000, 2, 6, 5 * 72 + 24);
  KextWrite32(Buffer + 0x4000 + 24, 0x80000000U);
  Segment(Buffer, 0x4020, "__TEXT", BASE + 0x4000, 0x4000, 0x1000, 1, 0);
  Segment(Buffer, 0x4068, "__TEXT_EXEC", BASE + 0x8000, 0x8000, 0x100, 5, 0);
  Segment(Buffer, 0x40B0, "__DATA", BASE + 0x18000, 0x18000, 0x100, 3, 0);
  Segment(Buffer, 0x40F8, "__LINKEDIT", BASE + 0x20000, 0x20000, 0x1000, 1, 0);
  Symbols(Buffer, 0x4140, 0x20000, "_provider", BASE + 0x8000, 0);
  Segment(Buffer, 0x4158, "__BSS", BASE + 0x18100, 0, 0x100, 3, 0);
  KextWrite64(Buffer + 0x4158 + 48, 0);
  KextWrite32(Buffer + 0x8000, 0xD65F03C0U);
  CopyMem(Buffer + 0xC000, Prelink, sizeof(Prelink));
  KextWrite64(Buffer + 0x18008, 0x8100ULL | (0xFFDULL << 51));
  KextWrite64(Buffer + 0x1BFFC, 0x8200);
  KextWrite32(Buffer + 0x20804, 28);
  KextWrite32(Buffer + 0x20808, 78);
  KextWrite32(Buffer + 0x2080C, 78);
  KextWrite32(Buffer + 0x20814, 1);
  KextWrite32(Buffer + 0x2081C, 5);
  KextWrite32(Buffer + 0x2082C, 24);
  KextWrite32(Buffer + 0x20834, 26);
  KextWrite32(Buffer + 0x20838, 0x00084000);
  KextWrite64(Buffer + 0x2083C, 0x18000);
  Buffer[0x20848] = 2;
  Buffer[0x2084A] = 8;
  Buffer[0x2084C] = Buffer[0x2084D] = 0xFF;
}

STATIC VOID
BuildKext(UINT8 *Buffer, CONST CHAR8 *Export, CONST CHAR8 *Import)
{
  UINTN At = 32;
  UINTN Chain = 0xC400;
  UINTN Starts = Chain + 28;
  UINTN Record = Starts + 20;
  UINTN Imports = Record + 24;
  UINTN Names = Imports + 4;
  ZeroMem(Buffer, KEXT_SIZE);
  Header(Buffer, 0, 0xB, 7, 3 * 152 + 72 + 24 + 16 + 16);
  Segment(Buffer, At, "__TEXT", 0, 0, 0x4000, 1, 0);
  At += 72;
  Segment(Buffer, At, "__TEXT_EXEC", 0x4000, 0x4000, 0x4000, 5, 1);
  Section(Buffer, At + 72, "__text", "__TEXT_EXEC", 0x4000, 0x4000, 0x20);
  At += 152;
  Segment(Buffer, At, "__DATA", 0x8000, 0x8000, 0x4000, 3, 1);
  Section(Buffer, At + 72, "__data", "__DATA", 0x8000, 0x8000, 0x100);
  At += 152;
  Segment(Buffer, At, "__LINKEDIT", 0xC000, 0xC000, 0x4000, 1, 0);
  At += 72;
  // Header command size is calculated from the actual sections above.
  KextWrite32(Buffer + 20, (UINT32)(At - 32 + 24 + 16 + 16));
  Symbols(Buffer, At, 0xC000, Export, 0x4000, 0x8000);
  At += 24;
  KextWrite32(Buffer + At, 0x80000034U);
  KextWrite32(Buffer + At + 4, 16);
  KextWrite32(Buffer + At + 8, (UINT32)Chain);
  KextWrite32(Buffer + At + 12, (UINT32)(Names + strlen(Import) + 1 - Chain));
  At += 16;
  KextWrite32(Buffer + At, 0x1E);
  KextWrite32(Buffer + At + 4, 16);
  KextWrite32(Buffer + At + 8, 0xC600);
  {
    STATIC CONST UINT8 Split[] = {0x7F, 3, 1, 2,  1,  0, 5,  5,    1, 0,  6, 1,    4, 3, 1,
                                  8,    4, 1, 16, 12, 1, 24, 1,    1, 1,  0, 1,    7, 1, 12,
                                  2,    1, 1, 0,  2,  2, 1,  0xB4, 1, 13, 1, 0xD0, 1};
    KextWrite32(Buffer + At + 12, sizeof(Split));
    CopyMem(Buffer + 0xC600, Split, sizeof(Split));
  }
  KextWrite32(Buffer + Chain + 4, 28);
  KextWrite32(Buffer + Chain + 8, (UINT32)(Imports - Chain));
  KextWrite32(Buffer + Chain + 12, (UINT32)(Names - Chain));
  KextWrite32(Buffer + Chain + 16, 1);
  KextWrite32(Buffer + Chain + 20, 1);
  KextWrite32(Buffer + Starts, 4);
  KextWrite32(Buffer + Starts + 12, 20);
  KextWrite32(Buffer + Record, 24);
  KextWrite32(Buffer + Record + 4, 0x00074000);
  KextWrite64(Buffer + Record + 8, 0x8000);
  Buffer[Record + 20] = 1;
  Buffer[Record + 22] = 180;
  CopyMem(Buffer + Names, Import, strlen(Import) + 1);
  KextWrite32(Buffer + 0x4000, 0x90000000U);
  KextWrite32(Buffer + 0x4004, 0x91000000U);
  KextWrite32(Buffer + 0x400C, 0x14000000U);
  KextWrite64(Buffer + 0x8000 + 180, 0x4000ULL | (2ULL << 51));
  KextWrite64(Buffer + 0x8000 + 188,
              (1ULL << 63) | (1ULL << 62) | (0x1234ULL << 32) | (1ULL << 48) | (5ULL << 51));
  KextWrite64(Buffer + 0x8000 + 208, (1ULL << 63) | 0x4000);
}

STATIC VOID
Release(AC_KERNEL_COLLECTION_CONTEXT *Context)
{
  VOID *Buffer = Context->Buffer;
  AcKernelCollectionFree(Context);
  if (Buffer != NULL)
    FreePool(Buffer);
}

STATIC VOID
Inputs(AC_KERNEL_KEXT Kexts[3], UINT8 *Alpha, UINT8 *Beta)
{
  AC_KERNEL_KEXT A = {"Alpha.kext", "Contents/MacOS/Alpha", AlphaInfo, sizeof(AlphaInfo) - 1, Alpha,
                      KEXT_SIZE};
  AC_KERNEL_KEXT B = {"Alpha.kext/Contents/PlugIns/Beta.kext",
                      "Contents/MacOS/Beta",
                      BetaInfo,
                      sizeof(BetaInfo) - 1,
                      Beta,
                      KEXT_SIZE};
  AC_KERNEL_KEXT C = {"Personality.kext", "", CodelessInfo, sizeof(CodelessInfo) - 1, NULL, 0};
  Kexts[0] = A;
  Kexts[1] = B;
  Kexts[2] = C;
}

STATIC VOID
ExpectFailure(AC_KERNEL_COLLECTION_CONTEXT *Source, AC_KERNEL_KEXT *Inputs, UINTN Count,
              EFI_STATUS Expected, UINTN ExpectedIndex)
{
  AC_KERNEL_COLLECTION_CONTEXT Result = {0};
  UINT8                       *Snapshot = malloc(Source->BufferSize);
  UINTN                        Outstanding = Pools;
  UINTN                        Failed = 0;
  EFI_STATUS                   Status;
  assert(Snapshot != NULL);
  memcpy(Snapshot, Source->Buffer, Source->BufferSize);
  Status = AcKernelCollectionInject(Source, Inputs, Count, &Result, &Failed);
  if (Status != Expected)
    fprintf(stderr, "Expected %llx, got %llx at %llu\n", (unsigned long long)Expected,
            (unsigned long long)Status, (unsigned long long)Failed);
  assert(Status == Expected);
  assert(Failed == ExpectedIndex);
  assert(Result.Buffer == NULL && Result.Entries == NULL);
  assert(memcmp(Snapshot, Source->Buffer, Source->BufferSize) == 0);
  assert(Pools == Outstanding);
  free(Snapshot);
}

STATIC VOID
TestBatch(AC_KERNEL_COLLECTION_CONTEXT *Source, AC_KERNEL_KEXT *Kexts)
{
  AC_KERNEL_COLLECTION_CONTEXT Result = {0};
  AC_KERNEL_LOAD_INFO          Load;
  AC_MACH_HEADER               Header;
  AC_MACH_COMMAND              Command;
  UINTN                        Failed;
  UINTN                        Index;
  UINTN                        At;
  UINTN                        Data[2] = {0};
  UINTN                        Code[2] = {0};
  UINTN                        Metadata = 0;
  UINT64                       Body;
  UINT8                       *Destination;
  AC_KERNEL_LOADED_IMAGE       Loaded;
  UINTN                        OriginalPools = Pools;
  EFI_STATUS Status = AcKernelCollectionInject(Source, Kexts, 3, &Result, &Failed);
  if (EFI_ERROR(Status))
    fprintf(stderr, "Batch failed: %llx, input %llu\n", (unsigned long long)Status,
            (unsigned long long)Failed);
  assert(Status == EFI_SUCCESS && Failed == MAX_UINTN);
  assert(Result.EntryCount == 3 && Source->EntryCount == 1);
  assert(memcmp(Result.Buffer + 0x4000, Source->Buffer + 0x4000, 0x1000) == 0);
  assert(memcmp(Result.Buffer + 0x8000, Source->Buffer + 0x8000, 0x100) == 0);
  assert((KextRead64(Result.Buffer + 0x18008) & ~(0xFFFULL << 51)) == 0x8100);
  assert((KextRead64(Result.Buffer + 0x1BFFC) & ~(0xFFFULL << 51)) == 0x8200);
  for (Index = 0; Index < 2; ++Index)
  {
    assert(InternalReadMachHeader(Result.Buffer, Result.BufferSize,
                                  Result.Entries[Index + 1].FileOffset, &Header) == EFI_SUCCESS);
    At = Result.Entries[Index + 1].FileOffset + 32;
    for (; At < Result.Entries[Index + 1].FileOffset + 32 + Header.CommandsSize; At += Command.Size)
    {
      CopyMem(&Command, Result.Buffer + At, 8);
      assert(Command.Type != 0x1E && Command.Type != 0x80000034U);
      if (Command.Type == 0x19 && KextName((CONST CHAR8 *)Result.Buffer + At + 8, "__DATA"))
        Data[Index] = (UINTN)KextRead64(Result.Buffer + At + 40);
      if (Command.Type == 0x19 && KextName((CONST CHAR8 *)Result.Buffer + At + 8, "__TEXT_EXEC"))
        Code[Index] = (UINTN)KextRead64(Result.Buffer + At + 40);
    }
  }
  assert(Data[0] != 0 && Data[1] != Data[0] && Code[0] != 0 && Code[1] != Code[0]);
  assert(Data[0] >= 0x18200 && Data[1] >= 0x18200);
  for (At = 0x18100; At < 0x18200; ++At)
    assert(Result.Buffer[At] == 0);
  for (Index = 0; Index < 2; ++Index)
  {
    UINT32 Adrp = KextRead32(Result.Buffer + Code[Index]);
    INT32  Pages = (INT32)((((Adrp >> 5) & 0x7FFFFU) << 2) | ((Adrp >> 29) & 3U));
    if ((Pages & 0x100000) != 0)
      Pages -= 0x200000;
    assert((INT64)(Code[Index] & ~0xFFFULL) + (INT64)Pages * 4096 ==
           (INT64)(Data[Index] & ~0xFFFULL));
    assert(((KextRead32(Result.Buffer + Code[Index] + 4) >> 10) & 0xFFFU) ==
           (Data[Index] & 0xFFFU));
    assert((INT32)KextRead32(Result.Buffer + Code[Index] + 8) ==
           (INT64)Data[Index] - (INT64)(Code[Index] + 8));
    assert((INT64)KextRead64(Result.Buffer + Code[Index] + 16) ==
           (INT64)Data[Index] - (INT64)(Code[Index] + 16));
    assert((KextRead32(Result.Buffer + Code[Index] + 12) & 0x3FFFFFFU) == 0x3FFFFFDU);
    assert(KextRead32(Result.Buffer + Code[Index] + 24) ==
           Data[Index] - Result.Entries[Index + 1].FileOffset);
    assert((KextRead64(Result.Buffer + Data[Index] + 208) & 0x3FFFFFFFULL) == Code[Index]);
  }
  Body = KextRead64(Result.Buffer + Data[0] + 188);
  assert((Body & 0x3FFFFFFFULL) == 0x8000 &&
         (Body & 0x8007FFFF00000000ULL) == 0x8001123400000000ULL);
  Body = KextRead64(Result.Buffer + Data[1] + 188);
  assert((Body & 0x3FFFFFFFULL) == Code[0]);
  assert(KextRead64(Result.Buffer + Data[0] + 156) == 0);
  for (At = 32; At < 32 + KextRead32(Result.Buffer + 20); At += Command.Size)
  {
    CopyMem(&Command, Result.Buffer + At, 8);
    if (Command.Type == 0x19 && KextName((CONST CHAR8 *)Result.Buffer + At + 8, "__PRELINK_INFO"))
      Metadata = (UINTN)KextRead64(Result.Buffer + At + 40);
  }
  assert(Metadata != 0);
  assert(strstr((CHAR8 *)Result.Buffer + Metadata, "org.example.Personality") != NULL);
  assert(strstr((CHAR8 *)Result.Buffer + Metadata, "<integer>-1</integer>") != NULL);
  assert(strstr((CHAR8 *)Result.Buffer + Metadata, "A&amp;B") != NULL);
  assert(strstr((CHAR8 *)Result.Buffer + Metadata, "_PrelinkExecutableRelativePath") != NULL);
  assert(AcKernelGetLoadInfo(&Result, AC_KEXT_LIMIT, &Load) == EFI_SUCCESS);
  Destination = malloc(Load.MemorySize);
  assert(Destination != NULL);
  assert(AcKernelLoadCollection(&Result, Destination, Load.MemorySize,
                                0x80000000ULL + (Load.VirtualBase & 0x1FFFFFFU),
                                &Loaded) == EFI_SUCCESS);
  assert(memcmp(Destination + Data[0], Result.Buffer + Data[0], 256) == 0);
  free(Destination);
  Release(&Result);
  assert(Pools == OriginalPools);
}

STATIC UINT32
BranchInstruction(UINTN From, UINTN To)
{
  return 0x94000000U | ((UINT32)(((INTN)To - (INTN)From) / 4) & 0x3FFFFFFU);
}

STATIC UINT32
PageInstruction(UINTN From, UINTN To, UINT32 Register)
{
  INTN Pages = ((INTN)(To & ~0xFFFULL) - (INTN)(From & ~0xFFFULL)) / 4096;
  return 0x90000000U | (((UINT32)Pages & 3U) << 29) | (((UINT32)Pages & 0x1FFFFCU) << 3) | Register;
}

STATIC VOID
BuildRxRoot(UINT8 *Buffer)
{
  // A synthetic kext-first collection with two recoverable catalog pages.
  STATIC CONST UINT32 MappingCall[] = {
    0,           0xF9400100U, 0, 0xF9400701U, 0xD2800002U, 0x52800003U, 0x52800044U,
    0x52800005U, 0x52800066U, 0, 0xF940038AU, 0xF940070BU, 0xAA0B014BU, 0xB400004BU};
  // Record both mapper invocations without touching any page tables.
  STATIC CONST UINT32 Mapper[] = {0,           0x91000129U, 0xF940012AU, 0x9100412BU,
                                  0x8B0A196BU, 0xA9000560U, 0xA9010D62U, 0xA9021564U,
                                  0xF9001966U, 0x9100054AU, 0xF900012AU, 0xD65F03C0U};
  BuildRoot(Buffer);
  KextWrite64(Buffer + 32 + 5 * 72 + 56 + 16 + 256, BASE + 0x9000);
  memset(Buffer + 0x8000, 0xA5, 0x1000);
  Header(Buffer, 0x4000, 2, 7, 6 * 72 + 24);
  KextWrite32(Buffer + 0x4018, AC_MACH_DYLIB_IN_CACHE);
  Segment(Buffer, 0x4068, "__TEXT_EXEC", BASE + 0x9000, 0x9000, 0x1000, 5, 0);
  KextWrite64(Buffer + 0x4068 + 48, 0x800);
  KextWrite64(Buffer + 0x20008, BASE + 0x9000);
  Segment(Buffer, 0x41A0, "__LAST", BASE + 0xA000, 0xA000, 0x2000, 5, 0);
  KextWrite32(Buffer + 0x9000, 0xD65F03C0U);
  CopyMem(Buffer + 0x9100, MappingCall, sizeof(MappingCall));
  KextWrite32(Buffer + 0x9100, PageInstruction(0x9100, 0x18000, 8));
  KextWrite32(Buffer + 0x9108, PageInstruction(0x9108, 0x18000, 24));
  KextWrite32(Buffer + 0x9124, BranchInstruction(0x9124, 0x9300));
  CopyMem(Buffer + 0x9300, Mapper, sizeof(Mapper));
  KextWrite32(Buffer + 0x9300, PageInstruction(0x9300, 0x1F000, 9));
}

STATIC VOID
TestRxMapping(VOID)
{
  UINT8                       *Root = AllocateZeroPool(ROOT_SIZE);
  UINT8                       *Alpha = AllocateZeroPool(KEXT_SIZE);
  UINT8                       *Beta = AllocateZeroPool(KEXT_SIZE);
  AC_KERNEL_COLLECTION_CONTEXT Source = {0}, Result = {0};
  AC_KERNEL_KEXT               Kexts[3];
  UINTN                        Failed, Index, Metadata = 0, Cave = 0x9800;
  UINTN                        CodeStart[2] = {0}, CodeSize[2] = {0};
  UINT32                       OriginalCall;
  UINT64                       TailSize;
  EFI_STATUS                   Status;
  assert(Root && Alpha && Beta);
  BuildRxRoot(Root);
  BuildKext(Alpha, "_alpha", "_provider");
  BuildKext(Beta, "_beta", "_alpha");
  // Each text section fits a page, but the batch needs more than one page.
  KextWrite64(Alpha + 32 + 72 + 72 + 40, 0x2800);
  KextWrite64(Beta + 32 + 72 + 72 + 40, 0x2800);
  Inputs(Kexts, Alpha, Beta);
  assert(AcKernelCollectionInit(&Source, Root, ROOT_SIZE, ROOT_SIZE) == EFI_SUCCESS);
  OriginalCall = KextRead32(Root + 0x9124);
  Status = AcKernelCollectionInject(&Source, Kexts, 3, &Result, &Failed);
  if (EFI_ERROR(Status))
    fprintf(stderr, "RX batch failed: %llx, input %llu\n", (unsigned long long)Status,
            (unsigned long long)Failed);
  assert(Status == EFI_SUCCESS);
  assert(Failed == MAX_UINTN && KextRead32(Root + 0x9124) == OriginalCall);
  assert(KextRead32(Result.Buffer + 0x9124) == BranchInstruction(0x9124, Cave));
  assert(KextRead32(Result.Buffer + Cave + 4) == BranchInstruction(Cave + 4, 0x9300));
  assert(KextRead32(Result.Buffer + Cave + 44) == BranchInstruction(Cave + 44, 0x9300));
  assert(KextRead64(Result.Buffer + 0x4068 + 32) == 0x1000);
  assert(KextRead64(Result.Buffer + 0x4068 + 48) == 0x838);
  assert(memcmp(Root + 0x9300, Result.Buffer + 0x9300, 48) == 0);
  for (UINTN At = 32; At < 32 + KextRead32(Result.Buffer + 20);
       At += KextRead32(Result.Buffer + At + 4))
    if (KextRead32(Result.Buffer + At) == 0x19 &&
        KextName((CONST CHAR8 *)Result.Buffer + At + 8, "__PRELINK_INFO"))
      Metadata = (UINTN)(KextRead64(Result.Buffer + At + 24) - BASE);
  assert(Metadata == 0x14000);
  TailSize = ((KextRead32(Result.Buffer + Cave + 16) >> 5) & 0xFFFFU) |
             (((UINT64)(KextRead32(Result.Buffer + Cave + 20) >> 5) & 0xFFFFU) << 16);
  assert(TailSize == 0x8000);
  for (Index = 0; Index < 2; ++Index)
  {
    UINTN Header = Result.Entries[Index + 1].FileOffset;
    for (UINTN At = Header + 32; At < Header + 32 + KextRead32(Result.Buffer + Header + 20);
         At += KextRead32(Result.Buffer + At + 4))
      if (KextRead32(Result.Buffer + At) == 0x19 &&
          KextName((CONST CHAR8 *)Result.Buffer + At + 8, "__TEXT_EXEC"))
      {
        CodeStart[Index] = (UINTN)(KextRead64(Result.Buffer + At + 24) - BASE);
        CodeSize[Index] = (UINTN)KextRead64(Result.Buffer + At + 32);
      }
    assert(CodeStart[Index] >= 0xC000 && CodeStart[Index] + CodeSize[Index] <= Metadata);
  }
  assert(CodeStart[1] + CodeSize[1] > 0x10000);
#if defined(__aarch64__)
  {
    // Execute the emitted wrapper on the host with a harmless recording mapper.
    // Rebase the complete synthetic image so all PC-relative instructions survive.
    UINT8 *Image = mmap(NULL, 0x40000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    typedef VOID (*MAPPER)(UINT64, UINT64, UINT64, UINT64, UINT64, UINT64, UINT64);
    UINT64 *Records;
    assert(Image != MAP_FAILED);
    memcpy(Image, Result.Buffer, Result.BufferSize);
    __builtin___clear_cache((CHAR8 *)Image, (CHAR8 *)Image + 0x18000);
    assert(mprotect(Image, 0x18000, PROT_READ | PROT_EXEC) == 0);
    ((MAPPER)(VOID *)(Image + Cave))(0x111000, 0x8000, 0, 0, 2, 0, 3);
    Records = (UINT64 *)(VOID *)(Image + 0x1F000);
    assert(Records[0] == 2);
    assert(Records[2] == 0x111000 && Records[3] == 0x8000);
    assert(Records[10] == (UINT64)(UINTN)(Image + 0xC000) && Records[11] == 0x8000);
    for (Index = 0; Index < 2; ++Index)
    {
      UINT64 *Arguments = Records + 2 + Index * 8;
      assert(Arguments[2] == 0 && Arguments[3] == 0 && Arguments[4] == 2 && Arguments[5] == 0 &&
             Arguments[6] == 3);
    }
    assert(munmap(Image, 0x40000) == 0);
  }
#endif
  Release(&Result);
  KextWrite32(Root + 0x9120, 0x52800026U);
  ExpectFailure(&Source, Kexts, 3, EFI_UNSUPPORTED, MAX_UINTN);
  KextWrite32(Root + 0x9120, 0x52800066U);
  memcpy(Root + 0x9400, Root + 0x9100, 56);
  KextWrite32(Root + 0x9424, BranchInstruction(0x9424, 0x9300));
  ExpectFailure(&Source, Kexts, 3, EFI_UNSUPPORTED, MAX_UINTN);
  memset(Root + 0x9400, 0, 56);
  KextWrite64(Root + 0x4068 + 48, 0xFE0);
  ExpectFailure(&Source, Kexts, 3, EFI_OUT_OF_RESOURCES, MAX_UINTN);
  KextWrite64(Root + 0x4068 + 48, 0x800);
  KextWrite64(Root + 0x41A0 + 32, 0x1FF0);
  KextWrite64(Root + 0x41A0 + 48, 0x1FF0);
  ExpectFailure(&Source, Kexts, 3, EFI_UNSUPPORTED, MAX_UINTN);
  KextWrite64(Root + 0x41A0 + 32, 0x2000);
  KextWrite64(Root + 0x41A0 + 48, 0x2000);
  Release(&Source);
  FreePool(Alpha);
  FreePool(Beta);
  printf("Kernel RX tail tests passed (multi-page batch, native call preservation, rollback).\n");
}

int
main(VOID)
{
  UINT8                       *Root = AllocateZeroPool(ROOT_SIZE);
  UINT8                       *Alpha = AllocateZeroPool(KEXT_SIZE);
  UINT8                       *Beta = AllocateZeroPool(KEXT_SIZE);
  AC_KERNEL_COLLECTION_CONTEXT Source = {0};
  AC_KERNEL_COLLECTION_CONTEXT Result = {0};
  AC_KERNEL_KEXT               Kexts[3];
  AC_KERNEL_KEXT               Swap;
  UINTN                        Index;
  UINTN                        Allocations;
  UINTN                        Failed;
  UINTN                        StartPools;
  assert(Root != NULL && Alpha != NULL && Beta != NULL);
  BuildRoot(Root);
  BuildKext(Alpha, "_alpha", "_provider");
  BuildKext(Beta, "_beta", "_alpha");
  Inputs(Kexts, Alpha, Beta);
  assert(AcKernelCollectionInit(&Source, Root, ROOT_SIZE, ROOT_SIZE) == EFI_SUCCESS);
  TestBatch(&Source, Kexts);
  assert(AcKernelCollectionInject(&Source, NULL, 0, &Result, &Failed) == EFI_SUCCESS);
  assert(Result.BufferSize == Source.BufferSize &&
         memcmp(Result.Buffer, Source.Buffer, Source.BufferSize) == 0);
  Release(&Result);
  assert(AcKernelCollectionInject(&Source, Kexts + 2, 1, &Result, &Failed) == EFI_SUCCESS);
  assert(Result.EntryCount == Source.EntryCount);
  Release(&Result);
  Swap = Kexts[0];
  Kexts[0] = Kexts[1];
  Kexts[1] = Swap;
  ExpectFailure(&Source, Kexts, 3, EFI_NOT_FOUND, 0);
  Inputs(Kexts, Alpha, Beta);
  {
    CHAR8  Incompatible[sizeof(BetaInfo)];
    CHAR8 *Version;
    CopyMem(Incompatible, BetaInfo, sizeof(BetaInfo));
    Version = strstr(Incompatible, "<key>org.example.Alpha</key><string>1.0");
    assert(Version != NULL);
    Version[strlen("<key>org.example.Alpha</key><string>")] = '2';
    Kexts[1].Plist = Incompatible;
    ExpectFailure(&Source, Kexts, 3, EFI_INCOMPATIBLE_VERSION, 1);
  }
  Inputs(Kexts, Alpha, Beta);
  memset(Root + 32 + KextRead32(Root + 20), 0xA5, 0x4000 - 32 - KextRead32(Root + 20));
  memset(Root + 0x5000, 0xA5, 0x3000);
  ExpectFailure(&Source, Kexts, 3, EFI_OUT_OF_RESOURCES, 0);
  memset(Root + 0x5000, 0, 0x3000);
  memset(Root + 32 + KextRead32(Root + 20), 0, 0x4000 - 32 - KextRead32(Root + 20));
  Inputs(Kexts, Alpha, Beta);
  Kexts[1] = Kexts[0];
  ExpectFailure(&Source, Kexts, 2, EFI_ALREADY_STARTED, 1);
  Inputs(Kexts, Alpha, Beta);
  Alpha[0xC600] = 0;
  ExpectFailure(&Source, Kexts, 3, EFI_UNSUPPORTED, 0);
  Alpha[0xC600] = 0x7F;
  KextWrite64(Alpha + 0x8000 + 180, 0xFFFFFFFFU);
  ExpectFailure(&Source, Kexts, 3, EFI_COMPROMISED_DATA, 0);
  BuildKext(Alpha, "_alpha", "_missing");
  ExpectFailure(&Source, Kexts, 3, EFI_NOT_FOUND, 0);
  BuildKext(Alpha, "_alpha", "_provider");
  StartPools = Pools;
  Calls = 0;
  assert(AcKernelCollectionInject(&Source, Kexts, 3, &Result, &Failed) == EFI_SUCCESS);
  Allocations = Calls;
  Release(&Result);
  for (Index = 1; Index <= Allocations; ++Index)
  {
    Calls = 0;
    FailAt = Index;
    assert(AcKernelCollectionInject(&Source, Kexts, 3, &Result, &Failed) == EFI_OUT_OF_RESOURCES);
    assert(Result.Buffer == NULL && Result.Entries == NULL && Pools == StartPools);
  }
  FailAt = 0;
  Release(&Source);
  FreePool(Alpha);
  FreePool(Beta);
  TestRxMapping();
  assert(Pools == 0);
  printf("Kernel injection tests passed (multi-kext linking, load mapping, rollback, %llu "
         "allocation failures).\n",
         (unsigned long long)Allocations);
  return 0;
}
