/** @file
  Bounded LZFSE stream inspection and decompression.
**/

#ifndef AC_COMPRESSION_LIB_H
#define AC_COMPRESSION_LIB_H

#include <Uefi.h>

typedef struct
{
  UINTN CompressedSize;
  UINTN DecompressedSize;
} AC_LZFSE_INFO;

/**
  Preflight block headers through the first LZFSE end-of-stream marker.

  Info is cleared on failure. CompressedSize includes the marker; any remaining
  source bytes are outside the stream. This does not validate entropy data.
  At most 65536 blocks are accepted. No allocations are performed.

  Unknown initial magic returns EFI_UNSUPPORTED, malformed blocks return
  EFI_COMPROMISED_DATA, and size overflow or too many blocks EFI_BAD_BUFFER_SIZE.
**/
EFI_STATUS
AcLzfseGetInfo(IN CONST VOID *Buffer, IN UINTN BufferSize, OUT AC_LZFSE_INFO *Info);

/**
  Decode exactly one complete LZFSE stream into a nonoverlapping caller buffer.

  The library allocates and releases its workspace. Input must have no trailing
  bytes. Every block must produce its declared size and the stream must end
  completely. A small destination returns EFI_BUFFER_TOO_SMALL. Other format
  errors return EFI_COMPROMISED_DATA or EFI_UNSUPPORTED; allocation failure
  returns EFI_OUT_OF_RESOURCES. Written is zero on failure, and destination
  contents are unspecified on failure. Invalid arguments return EFI_INVALID_PARAMETER.
**/
EFI_STATUS
AcDecompressLzfse(IN CONST VOID *Buffer, IN UINTN BufferSize, OUT VOID *Destination,
                  IN UINTN Capacity, OUT UINTN *Written);

#endif
