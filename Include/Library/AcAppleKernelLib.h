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

#endif
