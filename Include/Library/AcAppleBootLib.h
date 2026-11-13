/** @file
  Apple kernel boot memory preparation and argument encoding.
**/

#ifndef AC_APPLE_BOOT_LIB_H
#define AC_APPLE_BOOT_LIB_H

#include <Uefi.h>
#include <IndustryStandard/AppleBoot.h>
#include <Library/AcAppleKernelLib.h>
#include <Library/AcAppleAdtLib.h>

typedef struct AC_APPLE_BOOT_CONTEXT AC_APPLE_BOOT_CONTEXT;

typedef struct
{
  CONST VOID *Data;
  UINTN       Size;
} AC_APPLE_BOOT_TRUST_CACHE;

typedef struct
{
  CONST AC_KERNEL_COLLECTION_CONTEXT *Kernel;
  CONST AC_APPLE_BOOT_TRUST_CACHE    *TrustCaches;
  UINTN                               TrustCacheCount;
  UINTN                               DeviceTreeCapacity;
} AC_APPLE_BOOT_MEMORY_INPUT;

typedef struct
{
  CONST CHAR8        *CommandLine;
  AC_APPLE_BOOT_VIDEO Video;
  UINT32              MachineType;
  UINT64              BootFlags;
  UINT64              MemSizeActual;
} AC_APPLE_BOOT_ARGUMENTS;

typedef struct
{
  AC_APPLE_BOOT_MEMORY_RANGE Allocation;
  AC_APPLE_BOOT_MEMORY_RANGE ManagedMemory;
  UINT64                     AvailableMemory;
  UINT64                     VirtualBase;
  AC_KERNEL_LOADED_IMAGE     Kernel;
  AC_APPLE_BOOT_MEMORY_RANGE TrustCache;
  AC_APPLE_BOOT_MEMORY_RANGE DeviceTree;
  UINTN                      DeviceTreeSize;
  AC_APPLE_BOOT_MEMORY_RANGE BootArgs;
  UINT64                     BootArgsVirtual;
  EFI_PHYSICAL_ADDRESS       TopOfKernelData;
  BOOLEAN                    HasBootArgs;
} AC_APPLE_BOOT_INFO;

/**
  Select the largest eligible contiguous RAM range and load a patched Boot KC.

  Input and all referenced data are borrowed for the call. On success Context
  owns only Allocation and retains no input references. Release it with AcAppleBootFree().
  ManagedMemory describes the selected range; its unallocated tail stays under
  firmware control until EBS. DeviceTreeCapacity is at most 16 MiB; zero reserves no ADT.
  Up to 64 unwrapped version 1/2 trust cache modules are supported, in input order,
  with a 16 MiB aggregate limit. Structural validation does not authenticate them.

  Adjacent conventional descriptors with write-back capability are combined.
  Runtime, nonvolatile and special-purpose memory is excluded. Capability bits,
  including WP, are not interpreted as current access permissions. UEFI must
  provide readable and writable CPU mappings for allocated EfiLoaderData pages.
  ManagedMemory is trimmed to 32 MiB boundaries for the early mapping model.
  Only resident resource pages are allocated, excluding leading alignment space.
  AvailableMemory sums eligible
  RAM before trimming; it is not the installed capacity or an address span.
  The selection snapshot is retained privately. Revalidate the managed range
  against the final memory map before EBS; no page tables or CPU state are prepared.

  Failure clears Context and releases allocations. Invalid arguments return
  EFI_INVALID_PARAMETER, malformed modules EFI_COMPROMISED_DATA, unsupported
  module versions EFI_UNSUPPORTED, and size limits EFI_BAD_BUFFER_SIZE. No usable
  range returns EFI_OUT_OF_RESOURCES. Allocation failure does not select a smaller
  range. Firmware and KC errors are propagated;
  eight memory-map growth retries return EFI_ABORTED. Maps are limited to 16 MiB
  and 4096 descriptors. Requires Boot Services.
**/
EFI_STATUS
AcAppleBootPrepareMemory(IN CONST AC_APPLE_BOOT_MEMORY_INPUT *Input,
                         OUT AC_APPLE_BOOT_CONTEXT          **Context);

/**
  Serialize a borrowed ADT and encode the 0x480-byte, revision 2/version 2 ABI.

  Addresses are derived from Context. DeviceTree and Arguments must stay unchanged
  during the call; they are not retained or modified. CommandLine may be NULL for
  an empty string. Content longer than 1023 bytes returns EFI_BAD_BUFFER_SIZE.
  An ADT exceeding its reservation returns EFI_BUFFER_TOO_SMALL. Unknown boot
  flags return EFI_UNSUPPORTED. An empty Video is allowed; otherwise it describes
  a 32-bit framebuffer outside ManagedMemory. The caller supplies its format.

  On failure the previous serialized ADT, boot_args, and Info are unchanged.
  HasBootArgs means encoding succeeded, not that platform or CPU handoff is ready.
  ADT platform properties and referenced resource values are the caller's responsibility.
  Other errors follow ADT serialization and allocation. Requires Boot Services.
**/
EFI_STATUS
AcAppleBootWriteArgs(IN OUT AC_APPLE_BOOT_CONTEXT *Context, IN CONST AC_APPLE_ADT_TREE *DeviceTree,
                     IN CONST AC_APPLE_BOOT_ARGUMENTS *Arguments);

/**
  Copy a read-only layout snapshot without allocating. Kernel.Buffer borrows the
  context's allocation; never free it separately. DeviceTree.Length is reserved
  capacity; DeviceTreeSize is the actual encoded length, initially zero. Resource
  integer addresses are physical except VirtualBase, BootArgsVirtual, and Kernel.Info.
  Failure clears Info. A snapshot's pointers expire when Context is freed.
**/
EFI_STATUS
AcAppleBootGetInfo(IN CONST AC_APPLE_BOOT_CONTEXT *Context, OUT AC_APPLE_BOOT_INFO *Info);

/**
  Revalidate the encoded layout and managed RAM against a final raw UEFI map.

  This function allocates nothing and invokes no firmware services. MemoryMap
  is borrowed, may use a larger descriptor stride, and is not modified. The map
  must cover ManagedMemory without overlap or gaps. Only the context's allocation
  may be LoaderData there; other pages must be write-back conventional or Boot
  Services memory without runtime, nonvolatile or special-purpose attributes.
  Changed ownership returns EFI_ACCESS_DENIED. Malformed maps or inconsistent
  encoded arguments return EFI_COMPROMISED_DATA. Missing arguments return
  EFI_NOT_READY. Descriptor versions other than the UEFI version are unsupported.
**/
EFI_STATUS
AcAppleBootValidateHandoff(IN CONST AC_APPLE_BOOT_CONTEXT *Context, IN CONST VOID *MemoryMap,
                           IN UINTN MemoryMapSize, IN UINTN DescriptorSize,
                           IN UINT32 DescriptorVersion);

/** Release Allocation, the selection snapshot, and context before EBS. NULL is allowed. **/
VOID AcAppleBootFree(IN AC_APPLE_BOOT_CONTEXT *Context OPTIONAL);

#endif
