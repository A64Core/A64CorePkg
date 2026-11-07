/** @file
  Private Mach-O layouts and inspection helpers.
**/

#ifndef APPLE_KERNEL_INTERNAL_H
#define APPLE_KERNEL_INTERNAL_H

#include <Library/AcAppleKernelLib.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

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

#endif
