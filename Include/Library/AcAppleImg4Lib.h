/** @file
  Borrowed views of IMG4 and IM4P payloads.
**/

#ifndef AC_APPLE_IMG4_LIB_H
#define AC_APPLE_IMG4_LIB_H

#include <Uefi.h>

typedef enum
{
  AcImg4ContainerUnknown,
  AcImg4ContainerIm4p,
  AcImg4ContainerImg4
} AC_IMG4_CONTAINER_TYPE;

typedef struct
{
  AC_IMG4_CONTAINER_TYPE Container;
  CHAR8                  ComponentType[5];
  CONST UINT8           *Data;
  UINTN                  DataSize;
  BOOLEAN                HasCompression;
  UINT64                 CompressionAlgorithm;
  UINT64                 UncompressedSize;
  BOOLEAN                HasKeybag;
} AC_IMG4_PAYLOAD;

typedef struct
{
  CONST UINT8 *Data;
  UINTN        Size;
} AC_IMG4_MANIFEST;

/**
  Parse one complete DER-encoded IM4P or IMG4 without allocating or modifying it.

  Payload borrows Buffer, which must remain allocated and unchanged while the
  view is used. It is cleared on failure. Compression identifiers are returned
  without interpretation. Keybag presence does not authenticate or decrypt data.
  Optional manifests, restore information and properties are checked for bounded
  DER framing only; no signature, trust, or policy verification is performed.

  Invalid arguments return EFI_INVALID_PARAMETER. An unrecognized container or
  optional field returns EFI_UNSUPPORTED. Malformed supported structures return
  EFI_COMPROMISED_DATA. Opaque DER trees are limited to 16 levels and 65536 items;
  exceeding these limits returns EFI_BAD_BUFFER_SIZE.
**/
EFI_STATUS
AcImg4Parse(IN CONST VOID *Buffer, IN UINTN BufferSize, OUT AC_IMG4_PAYLOAD *Payload);

/**
  Borrow the complete IM4M DER from a standalone manifest or an IMG4 container.

  The view includes the outer SEQUENCE tag and length and is cleared on failure.
  Buffer must remain allocated and unchanged while the view is used. Framing,
  version and required fields are checked; signatures and policy are not verified.
  Valid IM4P or IMG4 without a manifest returns EFI_NOT_FOUND. Other errors follow
  AcImg4Parse().
**/
EFI_STATUS
AcImg4GetManifest(IN CONST VOID *Buffer, IN UINTN BufferSize, OUT AC_IMG4_MANIFEST *Manifest);

#endif
