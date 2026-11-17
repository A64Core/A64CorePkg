/** @file
  Private Mach-O layouts and inspection helpers.
**/

#ifndef APPLE_KERNEL_INTERNAL_H
#define APPLE_KERNEL_INTERNAL_H

#include <Library/AcAppleKernelLib.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/AcPlistLib.h>

#define AC_MACH_MAGIC_64 0xFEEDFACFU
#define AC_MACH_CPU_ARM64 0x0100000CU
#define AC_MACH_EXECUTE 0x2U
#define AC_MACH_KEXT_BUNDLE 0xBU
#define AC_MACH_FILESET 0xCU
#define AC_MACH_LC_SYMTAB 0x2U
#define AC_MACH_LC_UNIXTHREAD 0x5U
#define AC_MACH_LC_SEGMENT_64 0x19U
#define AC_MACH_LC_MAIN 0x80000028U
#define AC_MACH_LC_DYLD_CHAINED_FIXUPS 0x80000034U
#define AC_MACH_LC_FILESET_ENTRY 0x80000035U
#define AC_MACH_SECTION_64_SIZE 80U
#define AC_MACH_DYLIB_IN_CACHE 0x80000000U
#define AC_MACH_VM_PROT_EXECUTE 0x4U
#define AC_MACH_SG_NORELOC 0x4U
#define AC_MACH_SG_READ_ONLY 0x10U
#define AC_ARM_THREAD_STATE64 6U

// These fixed-width layouts follow Apple's mach-o/loader.h.
typedef struct
{
  UINT32 Magic;
  UINT32 CpuType;
  UINT32 CpuSubtype;
  UINT32 FileType;
  UINT32 CommandCount;
  UINT32 CommandsSize;
  UINT32 Flags;
  UINT32 Reserved;
} AC_MACH_HEADER;

typedef struct
{
  UINT32 Type;
  UINT32 Size;
} AC_MACH_COMMAND;

typedef struct
{
  AC_MACH_COMMAND Command;
  CHAR8           Name[16];
  UINT64          VirtualAddress;
  UINT64          VirtualSize;
  UINT64          FileOffset;
  UINT64          FileSize;
  UINT32          MaximumProtection;
  UINT32          InitialProtection;
  UINT32          SectionCount;
  UINT32          Flags;
} AC_MACH_SEGMENT;

typedef struct
{
  AC_MACH_COMMAND Command;
  UINT64          VirtualAddress;
  UINT64          FileOffset;
  UINT32          IdentifierOffset;
  UINT32          Reserved;
} AC_MACH_FILESET_ENTRY;

typedef struct
{
  AC_MACH_COMMAND Command;
  UINT32          SymbolsOffset;
  UINT32          SymbolCount;
  UINT32          StringsOffset;
  UINT32          StringsSize;
} AC_MACH_SYMTAB;

typedef struct
{
  UINT64 X[29];
  UINT64 Fp;
  UINT64 Lr;
  UINT64 Sp;
  UINT64 Pc;
  UINT32 Cpsr;
  UINT32 Reserved;
} AC_MACH_ARM_THREAD_STATE64;

typedef struct
{
  AC_MACH_COMMAND            Command;
  UINT32                     Flavor;
  UINT32                     Count;
  AC_MACH_ARM_THREAD_STATE64 State;
} AC_MACH_UNIXTHREAD;

typedef struct
{
  AC_MACH_COMMAND Command;
  UINT32          DataOffset;
  UINT32          DataSize;
} AC_MACH_LINKEDIT_DATA;

typedef struct
{
  UINT32 StringIndex;
  UINT8  Type;
  UINT8  Section;
  UINT16 Description;
  UINT64 Value;
} AC_MACH_SYMBOL;

typedef struct
{
  UINTN Offset;
  UINTN Size;
} AC_KERNEL_FILE_RANGE;

typedef struct
{
  UINT8                *Buffer;
  UINTN                 HeaderOffset;
  AC_MACH_SEGMENT      *Segments;
  UINT32                SegmentCount;
  AC_KERNEL_FILE_RANGE *Ranges;
  UINTN                 RangeCount;
  AC_KERNEL_FILE_RANGE *Excluded;
  UINTN                 ExcludedCount;
  AC_MACH_SYMTAB        Symtab;
  BOOLEAN               HasSymtab;
} AC_PATCHER_CONTEXT;

STATIC_ASSERT(sizeof(AC_MACH_HEADER) == 32, "Invalid Mach-O header layout");
STATIC_ASSERT(sizeof(AC_MACH_COMMAND) == 8, "Invalid Mach-O command layout");
STATIC_ASSERT(sizeof(AC_MACH_SEGMENT) == 72, "Invalid Mach-O segment layout");
STATIC_ASSERT(sizeof(AC_MACH_FILESET_ENTRY) == 32, "Invalid Mach-O fileset entry layout");
STATIC_ASSERT(sizeof(AC_MACH_SYMTAB) == 24, "Invalid Mach-O symbol command layout");
STATIC_ASSERT(sizeof(AC_MACH_SYMBOL) == 16, "Invalid Mach-O symbol layout");
STATIC_ASSERT(sizeof(AC_MACH_ARM_THREAD_STATE64) == 272, "Invalid ARM64 thread state layout");
STATIC_ASSERT(sizeof(AC_MACH_UNIXTHREAD) == 288, "Invalid Mach-O thread command layout");
STATIC_ASSERT(sizeof(AC_MACH_LINKEDIT_DATA) == 16, "Invalid Mach-O linkedit data layout");

/**
  Read a header and validate command framing and segment ranges.

  HeaderOffset locates a Mach-O header within Buffer. Segment file offsets remain
  relative to Buffer, including for images embedded in a fileset.
**/
EFI_STATUS
InternalReadMachHeader(IN CONST UINT8 *Buffer, IN UINTN BufferSize, IN UINTN HeaderOffset,
                       OUT AC_MACH_HEADER *Header);

/** Initialize an owned-range view of a validated collection entry. **/
EFI_STATUS
InternalInitPatcher(IN CONST AC_KERNEL_COLLECTION_CONTEXT *Collection, IN UINT32 EntryIndex,
                    OUT AC_PATCHER_CONTEXT *Patcher);

VOID InternalFreePatcher(IN OUT AC_PATCHER_CONTEXT *Patcher);

/** Return a target virtual address's file offset, rejecting ambiguous mappings. **/
EFI_STATUS
InternalPatcherAddressToOffset(IN CONST AC_PATCHER_CONTEXT *Patcher, IN UINT64 Address,
                               OUT UINTN *Offset);

/** Apply a binary patch to the target's owned file ranges. **/
EFI_STATUS
InternalApplyKernelPatch(IN CONST AC_PATCHER_CONTEXT *Patcher, IN CONST AC_KERNEL_PATCH *Patch,
                         OUT UINT32 *AppliedCount);

#define AC_KEXT_PAGE 0x4000U
#define AC_KEXT_LIMIT (512U * 1024U * 1024U)
#define AC_KEXT_SEGMENTS 32U
#define AC_KEXT_SECTIONS 256U
#define AC_KEXT_ROOTS 256U

typedef struct
{
  CHAR8  Name[16];
  CHAR8  Segment[16];
  UINT64 Address;
  UINT64 Size;
  UINT32 Offset;
  UINT32 Alignment;
  UINT32 Relocations;
  UINT32 RelocationCount;
  UINT32 Flags;
  UINT32 Reserved[3];
} AC_KEXT_SECTION;

typedef struct
{
  AC_MACH_SEGMENT Mach;
  UINTN           Command;
} AC_KEXT_ROOT;

typedef struct
{
  AC_MACH_SEGMENT Mach;
  UINTN           Command;
  UINT64          Address;
  UINTN           Offset;
  UINTN           Size;
} AC_KEXT_SEGMENT;

typedef struct
{
  AC_PLIST_DOCUMENT    *Document;
  CONST AC_PLIST_NODE  *Info;
  CONST AC_PLIST_NODE  *Libraries;
  CONST CHAR8          *Identifier;
  CONST UINT8          *Input;
  UINTN                 InputSize;
  AC_MACH_HEADER        Header;
  AC_KEXT_SEGMENT       Segments[AC_KEXT_SEGMENTS];
  AC_KEXT_SECTION       Sections[AC_KEXT_SECTIONS];
  UINTN                 SegmentCount;
  UINTN                 SectionCount;
  AC_MACH_SYMTAB        Symbols;
  AC_MACH_LINKEDIT_DATA Chains;
  AC_MACH_LINKEDIT_DATA Split;
  UINTN                 HeaderOffset;
  UINT64                HeaderAddress;
  UINT64                Kmod;
  UINTN                 TextSize;
  UINTN                 SymbolOffset;
  UINTN                 SymbolCount;
  UINTN                 StringOffset;
  UINTN                 StringSize;
  UINTN                 FixupStart;
} AC_KEXT_IMAGE;

typedef struct
{
  UINTN  Offset;
  UINT64 Body;
} AC_KEXT_FIXUP;

typedef struct
{
  CONST AC_KERNEL_COLLECTION_CONTEXT *Source;
  UINT8                              *Buffer;
  UINTN                               Size;
  UINTN                               Capacity;
  UINT64                              Base;
  AC_KEXT_ROOT                        Roots[AC_KEXT_ROOTS];
  UINTN                               RootCount;
  UINTN                               InfoRoot;
  UINTN                               LinkRoot;
  UINTN                               ChainsCommand;
  UINTN                               HeaderEnd;
  UINTN                               HeaderLimit;
  AC_PLIST_DOCUMENT                  *Document;
  CONST AC_PLIST_NODE                *Prelink;
  AC_KEXT_IMAGE                      *Images;
  CONST AC_KERNEL_KEXT               *Kexts;
  UINTN                               Count;
  AC_KERNEL_FILE_RANGE               *Used;
  UINTN                               UsedCount;
  UINTN                               UsedCapacity;
  AC_KEXT_FIXUP                      *Fixups;
  UINTN                               FixupCount;
  UINTN                               FixupCapacity;
} AC_KEXT_CONTEXT;

STATIC_ASSERT(sizeof(AC_KEXT_SECTION) == 80, "Invalid section layout");

STATIC inline UINT32
KextRead32(CONST VOID *Data)
{
  UINT32 Value;
  CopyMem(&Value, Data, sizeof(Value));
  return Value;
}
STATIC inline UINT16
KextRead16(CONST VOID *Data)
{
  UINT16 Value;
  CopyMem(&Value, Data, sizeof(Value));
  return Value;
}
STATIC inline UINT64
KextRead64(CONST VOID *Data)
{
  UINT64 Value;
  CopyMem(&Value, Data, sizeof(Value));
  return Value;
}
STATIC inline VOID
KextWrite32(VOID *Data, UINT32 Value)
{
  CopyMem(Data, &Value, sizeof(Value));
}
STATIC inline VOID
KextWrite64(VOID *Data, UINT64 Value)
{
  CopyMem(Data, &Value, sizeof(Value));
}
STATIC inline BOOLEAN
KextRange(UINTN Size, UINT64 Offset, UINT64 Length)
{
  return Offset <= Size && Length <= Size - Offset;
}
STATIC inline BOOLEAN
KextName(CONST CHAR8 Name[16], CONST CHAR8 *Text)
{
  UINTN Length = AsciiStrLen(Text);
  return Length < 16 && CompareMem(Name, Text, Length) == 0 && Name[Length] == '\0';
}

CONST AC_PLIST_NODE *InternalKextProperty(CONST AC_PLIST_NODE *Dictionary, CONST CHAR8 *Name);
EFI_STATUS           InternalKextParse(AC_KEXT_IMAGE *Image);
EFI_STATUS           InternalKextPlace(AC_KEXT_CONTEXT *Context, AC_KEXT_IMAGE *Image);
EFI_STATUS           InternalKextLink(AC_KEXT_CONTEXT *Context, AC_KEXT_IMAGE *Image, UINTN Index);
EFI_STATUS           InternalKextResolve(AC_KEXT_CONTEXT *Context, UINTN Index, CONST CHAR8 *Name,
                                         UINT64 *Address);
EFI_STATUS           InternalKextMap(AC_KEXT_IMAGE *Image, UINT64 Old, UINTN Length, UINTN *Offset,
                                     UINT64 *Address);
EFI_STATUS InternalKextAllocate(AC_KEXT_CONTEXT *Context, CONST CHAR8 *RootName, UINTN Size,
                                UINTN Alignment, UINTN *Offset, UINT64 *Address);
EFI_STATUS InternalKextAddFixup(AC_KEXT_CONTEXT *Context, UINTN Offset, UINT64 Body);

#endif
