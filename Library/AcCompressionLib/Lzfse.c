/** @file
  Strict LZFSE framing and an EDK II adapter for Apple's decoder.
**/

#include <Library/AcCompressionLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

#ifndef NDEBUG
#define NDEBUG
#endif

// Keep upstream header decoding private and share its validation with preflight.
#include "../../ThirdParty/lzfse/src/lzfse_decode_base.c"
#include "../../ThirdParty/lzfse/src/lzfse_fse.c"
#include "../../ThirdParty/lzfse/src/lzvn_decode_base.c"

STATIC EFI_STATUS
ReadBlock(CONST UINT8 *Buffer, UINTN Size, UINTN *BlockSize, UINTN *RawSize)
{
  UINT32                           Magic;
  UINTN                            HeaderSize;
  UINTN                            PayloadSize;
  lzfse_compressed_block_header_v1 Header;

  if (Size < 4)
    return EFI_COMPROMISED_DATA;
  Magic = load4(Buffer);
  if (Magic == LZFSE_ENDOFSTREAM_BLOCK_MAGIC)
  {
    *BlockSize = 4;
    *RawSize = 0;
    return EFI_SUCCESS;
  }
  if (Size < 8)
    return EFI_COMPROMISED_DATA;
  *RawSize = load4(Buffer + 4);
  if (Magic == LZFSE_UNCOMPRESSED_BLOCK_MAGIC)
  {
    HeaderSize = sizeof(uncompressed_block_header);
    PayloadSize = *RawSize;
  }
  else if (Magic == LZFSE_COMPRESSEDLZVN_BLOCK_MAGIC)
  {
    HeaderSize = sizeof(lzvn_compressed_block_header);
    if (Size < HeaderSize)
      return EFI_COMPROMISED_DATA;
    PayloadSize = load4(Buffer + 8);
  }
  else if ((Magic == LZFSE_COMPRESSEDV1_BLOCK_MAGIC) || (Magic == LZFSE_COMPRESSEDV2_BLOCK_MAGIC))
  {
    if (Magic == LZFSE_COMPRESSEDV1_BLOCK_MAGIC)
    {
      HeaderSize = sizeof(Header);
      if (Size < HeaderSize)
        return EFI_COMPROMISED_DATA;
      CopyMem(&Header, Buffer, sizeof(Header));
    }
    else
    {
      if (Size < offsetof(lzfse_compressed_block_header_v2, freq))
        return EFI_COMPROMISED_DATA;
      HeaderSize = load4(Buffer + 24);
      if ((HeaderSize < offsetof(lzfse_compressed_block_header_v2, freq)) ||
          (HeaderSize > sizeof(lzfse_compressed_block_header_v2)) || (HeaderSize > Size) ||
          (lzfse_decode_v1(&Header, (CONST lzfse_compressed_block_header_v2 *)Buffer) != 0))
        return EFI_COMPROMISED_DATA;
    }
    if ((lzfse_check_block_header_v1(&Header) != 0) || ((Header.n_literals & 3U) != 0) ||
        (Header.literal_bits < -7) || (Header.literal_bits > 0) || (Header.lmd_bits < -7) ||
        (Header.lmd_bits > 0) ||
        ((UINT64)Header.n_literal_payload_bytes + Header.n_lmd_payload_bytes !=
         Header.n_payload_bytes))
      return EFI_COMPROMISED_DATA;
    PayloadSize = Header.n_payload_bytes;
  }
  else
    return EFI_UNSUPPORTED;
  if ((HeaderSize > Size) || (PayloadSize > Size - HeaderSize))
    return EFI_COMPROMISED_DATA;
  *BlockSize = HeaderSize + PayloadSize;
  return EFI_SUCCESS;
}

EFI_STATUS
AcLzfseGetInfo(IN CONST VOID *Buffer, IN UINTN BufferSize, OUT AC_LZFSE_INFO *Info)
{
  EFI_STATUS   Status;
  CONST UINT8 *Bytes;
  UINTN        Offset;
  UINTN        RawSize;
  UINTN        BlockSize;
  UINTN        Total;
  UINTN        Count;

  if (Info == NULL)
    return EFI_INVALID_PARAMETER;
  ZeroMem(Info, sizeof(*Info));
  if (Buffer == NULL)
    return EFI_INVALID_PARAMETER;
  Bytes = Buffer;
  if ((BufferSize < 4) || ((load4(Bytes) != LZFSE_UNCOMPRESSED_BLOCK_MAGIC) &&
                           (load4(Bytes) != LZFSE_COMPRESSEDLZVN_BLOCK_MAGIC) &&
                           (load4(Bytes) != LZFSE_COMPRESSEDV1_BLOCK_MAGIC) &&
                           (load4(Bytes) != LZFSE_COMPRESSEDV2_BLOCK_MAGIC) &&
                           (load4(Bytes) != LZFSE_ENDOFSTREAM_BLOCK_MAGIC)))
    return EFI_UNSUPPORTED;
  Offset = 0;
  Total = 0;
  for (Count = 0; Count < 65536; ++Count)
  {
    Status = ReadBlock(Bytes + Offset, BufferSize - Offset, &BlockSize, &RawSize);
    if (EFI_ERROR(Status))
      return Status == EFI_UNSUPPORTED ? EFI_COMPROMISED_DATA : Status;
    if (RawSize > MAX_UINTN - Total)
      return EFI_BAD_BUFFER_SIZE;
    Total += RawSize;
    if (load4(Bytes + Offset) == LZFSE_ENDOFSTREAM_BLOCK_MAGIC)
    {
      Info->CompressedSize = Offset + BlockSize;
      Info->DecompressedSize = Total;
      return EFI_SUCCESS;
    }
    Offset += BlockSize;
  }
  return EFI_BAD_BUFFER_SIZE;
}

EFI_STATUS
AcDecompressLzfse(IN CONST VOID *Buffer, IN UINTN BufferSize, OUT VOID *Destination,
                  IN UINTN Capacity, OUT UINTN *Written)
{
  AC_LZFSE_INFO        Info;
  lzfse_decoder_state *State;
  EFI_STATUS           Status;
  CONST UINT8         *Bytes;
  UINT8               *Output;
  UINTN                Offset;
  UINTN                Used;
  UINTN                BlockSize;
  UINTN                RawSize;
  int                  Result;
  BOOLEAN              Last;

  if (Written == NULL)
    return EFI_INVALID_PARAMETER;
  *Written = 0;
  if ((Buffer == NULL) || (Destination == NULL) || (BufferSize > MAX_UINTN - (UINTN)Buffer) ||
      (Capacity > MAX_UINTN - (UINTN)Destination) ||
      (((UINTN)Buffer < (UINTN)Destination + Capacity) &&
       ((UINTN)Destination < (UINTN)Buffer + BufferSize)))
    return EFI_INVALID_PARAMETER;
  Status = AcLzfseGetInfo(Buffer, BufferSize, &Info);
  if (EFI_ERROR(Status))
    return Status;
  if (Info.CompressedSize != BufferSize)
    return EFI_COMPROMISED_DATA;
  if (Info.DecompressedSize > Capacity)
    return EFI_BUFFER_TOO_SMALL;
  State = AllocateZeroPool(sizeof(*State));
  if (State == NULL)
    return EFI_OUT_OF_RESOURCES;
  Bytes = Buffer;
  Output = Destination;
  State->src = State->src_begin = Bytes;
  State->dst = State->dst_begin = Output;
  Offset = 0;
  Used = 0;
  while (Offset < BufferSize)
  {
    Status = ReadBlock(Bytes + Offset, BufferSize - Offset, &BlockSize, &RawSize);
    if (EFI_ERROR(Status))
      break;
    Last = load4(Bytes + Offset) == LZFSE_ENDOFSTREAM_BLOCK_MAGIC;
    State->src_end = Bytes + Offset + BlockSize;
    State->dst_end = Output + Used + RawSize;
    // V2 blocks may reuse the preceding block's frequency tables.
    Result = lzfse_decode(State);
    if ((State->src != State->src_end) || (State->dst != State->dst_end) ||
        (Last
           ? ((Result != LZFSE_STATUS_OK) || !State->end_of_stream)
           : ((Result != LZFSE_STATUS_SRC_EMPTY) || (State->block_magic != LZFSE_NO_BLOCK_MAGIC))))
    {
      Status = EFI_COMPROMISED_DATA;
      break;
    }
    Offset += BlockSize;
    Used += RawSize;
  }
  FreePool(State);
  if (!EFI_ERROR(Status))
    *Written = Used;
  return Status;
}
