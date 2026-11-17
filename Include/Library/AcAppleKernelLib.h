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

typedef enum
{
  AcKernelContainerRaw,
  AcKernelContainerIm4p,
  AcKernelContainerImg4
} AC_KERNEL_CONTAINER_TYPE;

typedef struct
{
  AC_KERNEL_CONTAINER_TYPE Container;
  AC_KERNEL_PAYLOAD_TYPE   Type;
  BOOLEAN                  Compressed;
} AC_KERNEL_IMAGE_INFO;

/**
  Normalize an owned pool allocation into a writable, unpacked AArch64 image.

  Buffer must be an allocation releasable with FreePool(). Size is the readable
  file length and Capacity the allocation size. Raw Mach-O and unencrypted
  kernel IM4P/IMG4 components (uncompressed or LZFSE) are supported. Unknown auxiliary data,
  keybags and legacy comp wrappers are unsupported. No signature is verified.

  On success the caller owns the returned allocation. Uncompressed input is
  reused (wrapped data is moved to its beginning); compressed input is replaced
  and its original allocation freed. Size and Capacity describe the result.
  No spare space is reserved. An existing collection must not borrow Buffer.

  On failure Buffer, its bytes, Size and Capacity are unchanged; Info is cleared.
  MaxDecodedSize bounds the result before output allocation. Exceeding it returns
  EFI_BAD_BUFFER_SIZE. Other errors follow the container, decoder and Mach-O
  inspectors. This does not place segments, process fixups, or start the kernel.
**/
EFI_STATUS
AcKernelDecodePayload(IN OUT VOID **Buffer, IN OUT UINTN *Size, IN OUT UINTN *Capacity,
                      IN UINTN MaxDecodedSize, OUT AC_KERNEL_IMAGE_INFO *Info);

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

typedef struct
{
  UINT64 VirtualBase;
  UINT64 EntryPoint;
  UINTN  MemorySize;
  UINTN  Alignment;
} AC_KERNEL_LOAD_INFO;

typedef struct
{
  VOID                *Buffer;
  EFI_PHYSICAL_ADDRESS PhysicalBase;
  EFI_PHYSICAL_ADDRESS EntryPoint;
  AC_KERNEL_LOAD_INFO  Info;
} AC_KERNEL_LOADED_IMAGE;

/**
  Inspect the outer segments and ARM64 LC_UNIXTHREAD entry of a Boot KC.

  Context must be initialized and may have been patched through this library.
  VirtualBase and EntryPoint are unslid virtual addresses. MemorySize includes
  segment gaps and trailing 16 KiB rounding. Alignment is the required modulus:
  physical and virtual bases must have the same offset within 32 MiB.
  At most 256 outer segments and 65536 combined load commands are supported.
  File-backed padding checks across embedded zero-fill tails are bounded by the
  source file size. Other embedded segments must fit an outer segment's mapping.

  Info is cleared on failure. Unsupported layouts return EFI_UNSUPPORTED;
  inconsistent mappings or entry data return EFI_COMPROMISED_DATA. Exceeding
  MaxMemorySize or structural limits returns EFI_BAD_BUFFER_SIZE; temporary
  allocation failure returns EFI_OUT_OF_RESOURCES. Invalid arguments return
  EFI_INVALID_PARAMETER. No bytes in Context are changed.
**/
EFI_STATUS
AcKernelGetLoadInfo(IN CONST AC_KERNEL_COLLECTION_CONTEXT *Context, IN UINTN MaxMemorySize,
                    OUT AC_KERNEL_LOAD_INFO *Info);

/**
  Load a Boot KC into a nonoverlapping caller-owned destination without executing it.

  The layout is rechecked before any writes. Destination is a CPU-accessible
  pointer to Capacity bytes at PhysicalBase; the caller guarantees that mapping.
  Its image range must not overlap the source KC. Image must not alias either
  buffer. No virtual slide, fixups, pointer signing, page-table construction, or
  executable-cache preparation is performed. KC metadata is preserved for XNU.

  Image borrows Destination, which the caller must retain for handoff or release
  on cancellation. EntryPoint is physical; Info.EntryPoint is virtual. The source
  context and allocation remain owned by the caller and are not referenced by Image.
  Temporary allocations are released before returning. Failure leaves Destination
  unchanged and clears Image. In addition to inspection errors, a short destination
  returns EFI_BUFFER_TOO_SMALL; overlap, physical overflow or incompatible base
  alignment returns EFI_INVALID_PARAMETER.
**/
EFI_STATUS
AcKernelLoadCollection(IN CONST AC_KERNEL_COLLECTION_CONTEXT *Context, OUT VOID *Destination,
                       IN UINTN Capacity, IN EFI_PHYSICAL_ADDRESS PhysicalBase,
                       OUT AC_KERNEL_LOADED_IMAGE *Image);

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

typedef struct
{
  CONST CHAR8 *BundlePath;
  CONST CHAR8 *ExecutablePath;
  CONST VOID  *Plist;
  UINTN        PlistSize;
  CONST VOID  *Executable;
  UINTN        ExecutableSize;
} AC_KERNEL_KEXT;

/**
  Link a batch of ARM64e kext bundles into a Boot KC in configuration order.

  Source and inputs are borrowed and never modified. Empty executable paths
  describe plist-only bundles. Dependencies must exist in Source or earlier
  inputs. Duplicate IDs, missing/incompatible dependencies, unsupported formats
  and insufficient mapped capacity are errors. Existing member addresses and
  fixup targets remain stable. At most 1024 inputs are supported.

  Result must be a distinct, empty context. On success it owns its entry index
  and borrows a new buffer whose ownership passes to the caller. Release the
  index with AcKernelCollectionFree and the buffer separately with FreePool.
  Source retains its original ownership. Failure clears Result without changing
  Source. FailedIndex identifies an input, or MAX_UINTN for collection errors.
  For a supported kext-first ARM64 layout, a uniquely recognized native RX mapper
  is wrapped to retain its original call and map the page-aligned executable tail.
  Its entry uses zero padding in permanent kernel text; only that segment's file
  extent is extended. Unsupported or ambiguous mapping code rejects the batch.
  No kext ABI adapters or signature bypasses are installed.
**/
EFI_STATUS
AcKernelCollectionInject(IN CONST AC_KERNEL_COLLECTION_CONTEXT *Source,
                         IN CONST AC_KERNEL_KEXT *Kexts, IN UINTN Count,
                         OUT AC_KERNEL_COLLECTION_CONTEXT *Result, OUT UINTN *FailedIndex OPTIONAL);

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
