/** @file
  Apple kernel payload inspection and kernel collection support.
**/

#ifndef AC_APPLE_KERNEL_LIB_H
#define AC_APPLE_KERNEL_LIB_H

#include <Uefi.h>

typedef enum
{
  AcKernelPayloadUnknown,
  AcKernelPayloadMachO,
  AcKernelPayloadCollection
} AC_KERNEL_PAYLOAD_TYPE;

typedef struct
{
  CONST CHAR8 *Identifier;
  UINTN        FileOffset;
  UINT64       VirtualAddress;
} AC_KERNEL_COLLECTION_ENTRY;

/**
  An index of an unpacked AArch64 MH_FILESET kernel collection.

  Buffer remains owned by the caller. Entries is owned by the context, while
  entry identifiers reference Buffer. Keep Buffer allocated at the same address
  until AcKernelCollectionFree(). Only library operations may modify its contents.
  Treat context fields as read-only and do not shallow-copy a live context.
  Initialization, patching, and release require Boot Services.
**/
typedef struct
{
  UINT8                      *Buffer;
  UINTN                       BufferSize;
  UINTN                       BufferCapacity;
  AC_KERNEL_COLLECTION_ENTRY *Entries;
  UINT32                      EntryCount;
  UINT32                      KernelIndex;
} AC_KERNEL_COLLECTION_CONTEXT;

/**
  An equal-length binary patch.

  Replace is required and Size must be nonzero. Find, Mask, and ReplaceMask, when
  present, each reference Size bytes. Their lengths are checked by the caller
  before constructing this descriptor. All referenced data must remain immutable
  during the call and must not alias the payload being modified.

  NULL or empty Base starts at the target's Mach-O header. A nonempty Base names
  a defined symbol. Without Find, a nonempty Base is required and Replace is
  copied there once; masks, Count, Skip, and Limit are ignored in that mode.

  With Find, comparison is (Data & Mask) == Find, or exact without Mask. Bits in
  Find outside Mask must be zero to match. ReplaceMask selects bits to update.
  Count is a maximum (zero means all), Skip counts nonoverlapping matches, and
  Limit bounds file-coordinate bytes from the header or resolved Base (zero
  means the remaining target range). A skipped or replaced match advances Size.
**/
typedef struct
{
  CONST CHAR8 *Comment;
  CONST CHAR8 *Base;
  CONST UINT8 *Find;
  CONST UINT8 *Replace;
  CONST UINT8 *Mask;
  CONST UINT8 *ReplaceMask;
  UINT32       Size;
  UINT32       Count;
  UINT32       Skip;
  UINT32       Limit;
} AC_KERNEL_PATCH;

/**
  Identify an unpacked AArch64 MH_EXECUTE or MH_FILESET payload.

  Checks the Mach-O header, load-command framing, segment file ranges, and
  SYMTAB table bounds. This does not validate symbol contents, relocations,
  fixups, or bootability. Wrapped,
  compressed, fat, byte-swapped, and other architecture payloads are unsupported.

  @param[in]  Buffer      Payload bytes; alignment is not required.
  @param[in]  BufferSize  Number of readable bytes in Buffer.
  @param[out] Type        Payload type, or Unknown on failure.

  @retval EFI_SUCCESS            The supported payload was identified.
  @retval EFI_INVALID_PARAMETER  A required pointer is NULL.
  @retval EFI_UNSUPPORTED        The payload format or architecture is unsupported.
  @retval EFI_COMPROMISED_DATA   A supported Mach-O has invalid structural bounds.
**/
EFI_STATUS
AcKernelIdentifyPayload(IN CONST VOID *Buffer, IN UINTN BufferSize,
                        OUT AC_KERNEL_PAYLOAD_TYPE *Type);

/**
  Index a kernel collection containing exactly one com.apple.kernel entry.

  Context must not already own resources. On failure it is cleared. BufferSize
  is the payload length; BufferCapacity also includes any allocated spare space.
  Only BufferSize bytes are inspected. This function does not modify Buffer.

  @retval EFI_SUCCESS            The collection was indexed.
  @retval EFI_INVALID_PARAMETER  A pointer or buffer capacity is invalid.
  @retval EFI_UNSUPPORTED        A payload or embedded image format is unsupported.
  @retval EFI_COMPROMISED_DATA   The collection has invalid or ambiguous entries.
  @retval EFI_NOT_FOUND          The collection has no com.apple.kernel entry.
  @retval EFI_OUT_OF_RESOURCES   The entry index could not be allocated.
**/
EFI_STATUS
AcKernelCollectionInit(OUT AC_KERNEL_COLLECTION_CONTEXT *Context, IN OUT VOID *Buffer,
                       IN UINTN BufferSize, IN UINTN BufferCapacity);

/**
  Free the entry index and clear Context without freeing its payload buffer.

  Context must be initialized successfully or zero-initialized. NULL is allowed.
**/
VOID AcKernelCollectionFree(IN OUT AC_KERNEL_COLLECTION_CONTEXT *Context OPTIONAL);

/**
  Extract the Darwin version from the kernel's file-backed segments.

  Context must be successfully initialized; only library operations may have
  modified its payload or index since initialization.
  The numeric result is Major * 10000 + Minor * 100 + Revision. Each component
  must be in 0..99, and Major must be nonzero. XNU development, alpha, beta, and
  release suffixes are accepted and ignored.
  Repeated banners must agree. DarwinVersion is zero on failure.

  @retval EFI_SUCCESS            A consistent version was found.
  @retval EFI_INVALID_PARAMETER  A required pointer or context is invalid.
  @retval EFI_NOT_FOUND          No Darwin kernel version banner was found.
  @retval EFI_COMPROMISED_DATA   The kernel or a version banner is malformed.
  @retval EFI_UNSUPPORTED        The kernel image format is unsupported.
**/
EFI_STATUS
AcKernelReadDarwinVersion(IN CONST AC_KERNEL_COLLECTION_CONTEXT *Context,
                          OUT UINT32                            *DarwinVersion);

/**
  Apply one patch to the kernel (Identifier "kernel") or a fileset entry ID.

  Searches the target header and file-backed segments in ascending file order,
  starting at its Mach-O header or Base. Gaps and bytes also claimed by another
  image are excluded, including shared LINKEDIT data. Adjacent owned ranges may
  be matched across their boundary; gaps are never concatenated. Limit counts
  file-coordinate bytes, including gaps. Base uses LC_SYMTAB, falling back to
  the collection's LC_SYMTAB when the target has none. Defined N_SECT and N_ABS
  symbols are supported; zero-fill addresses have no patchable file contents.

  Writes are immediate and are not rolled back. At least one replacement is a
  success even if Count is not reached. AppliedCount reports writes even if a
  later error occurs. The root index and other images are protected from writes,
  so entry pointers remain valid after a successful call. The target header and
  commands are validated after writes. If they invalidate the collection,
  Context is cleared while the caller still owns the modified Buffer.
  Symbols and fixups are not rebuilt.

  @param[in,out] Context       Initialized collection context.
  @param[in]     Identifier    Target name; must be nonempty.
  @param[in]     Patch         Patch descriptor.
  @param[out]    AppliedCount  Actual replacements, or zero before any write.

  @retval EFI_SUCCESS            At least one replacement was made.
  @retval EFI_INVALID_PARAMETER  A pointer, context, or descriptor is invalid.
  @retval EFI_NOT_FOUND          No target, usable symbol, or replacement was found.
  @retval EFI_UNSUPPORTED        A matching symbol form or direct-write range is unsupported.
  @retval EFI_COMPROMISED_DATA   Metadata is malformed, or writes invalidated the collection.
  @retval EFI_OUT_OF_RESOURCES   Temporary patcher data could not be allocated.
**/
EFI_STATUS
AcKernelCollectionApplyPatch(IN OUT AC_KERNEL_COLLECTION_CONTEXT *Context,
                             IN CONST CHAR8 *Identifier, IN CONST AC_KERNEL_PATCH *Patch,
                             OUT UINT32 *AppliedCount OPTIONAL);

#endif
