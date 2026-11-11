/** @file
  Kernel container extraction, decompression and image normalization.
**/

#include <Library/AcAppleKernelLib.h>
#include <Library/AcAppleImg4Lib.h>
#include <Library/AcCompressionLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

EFI_STATUS
AcKernelDecodePayload(IN OUT VOID **Buffer, IN OUT UINTN *Size, IN OUT UINTN *Capacity,
                      IN UINTN MaxDecodedSize, OUT AC_KERNEL_IMAGE_INFO *Info)
{
  AC_KERNEL_IMAGE_INFO Result;
  AC_IMG4_PAYLOAD      Payload;
  AC_LZFSE_INFO        Stream;
  CONST UINT8         *Data;
  UINTN                DataSize;
  UINTN                Written;
  VOID                *Decoded;
  EFI_STATUS           Status;

  if (Info == NULL)
    return EFI_INVALID_PARAMETER;
  ZeroMem(Info, sizeof(*Info));
  if ((Buffer == NULL) || (*Buffer == NULL) || (Size == NULL) || (Capacity == NULL) ||
      (*Size > *Capacity) || (MaxDecodedSize == 0))
    return EFI_INVALID_PARAMETER;
  if (*Size == 0)
    return EFI_COMPROMISED_DATA;
  ZeroMem(&Result, sizeof(Result));
  Status = AcKernelIdentifyPayload(*Buffer, *Size, &Result.Type);
  if (Status == EFI_COMPROMISED_DATA)
    return Status;
  Data = *Buffer;
  DataSize = *Size;
  if (EFI_ERROR(Status))
  {
    Status = AcImg4Parse(*Buffer, *Size, &Payload);
    if (EFI_ERROR(Status))
      return Status;
    if (((CompareMem(Payload.ComponentType, "krnl", 4) != 0) &&
         (CompareMem(Payload.ComponentType, "rkrn", 4) != 0)) ||
        Payload.HasKeybag)
      return EFI_UNSUPPORTED;
    Result.Container =
      Payload.Container == AcImg4ContainerImg4 ? AcKernelContainerImg4 : AcKernelContainerIm4p;
    Data = Payload.Data;
    DataSize = Payload.DataSize;
    if (DataSize == 0)
      return EFI_COMPROMISED_DATA;
    if (Payload.HasCompression && (Payload.CompressionAlgorithm != 1))
      return EFI_UNSUPPORTED;
    if (Payload.HasCompression &&
        ((Payload.UncompressedSize == 0) || (Payload.UncompressedSize > MaxDecodedSize)))
      return Payload.UncompressedSize == 0 ? EFI_COMPROMISED_DATA : EFI_BAD_BUFFER_SIZE;
    Status = AcKernelIdentifyPayload(Data, DataSize, &Result.Type);
    if (Status == EFI_COMPROMISED_DATA)
      return Status;
    if (Payload.HasCompression || EFI_ERROR(Status))
    {
      Result.Compressed = TRUE;
      Status = AcLzfseGetInfo(Data, DataSize, &Stream);
      if (EFI_ERROR(Status))
        return Payload.HasCompression && Status == EFI_UNSUPPORTED ? EFI_COMPROMISED_DATA : Status;
      if (Stream.CompressedSize != DataSize)
        return EFI_UNSUPPORTED;
      if ((Stream.DecompressedSize == 0) ||
          (Payload.HasCompression && (Payload.UncompressedSize != Stream.DecompressedSize)))
        return EFI_COMPROMISED_DATA;
      if (Stream.DecompressedSize > MaxDecodedSize)
        return EFI_BAD_BUFFER_SIZE;
      Decoded = AllocatePool(Stream.DecompressedSize);
      if (Decoded == NULL)
        return EFI_OUT_OF_RESOURCES;
      Status = AcDecompressLzfse(Data, DataSize, Decoded, Stream.DecompressedSize, &Written);
      if (!EFI_ERROR(Status))
        Status = AcKernelIdentifyPayload(Decoded, Written, &Result.Type);
      if (EFI_ERROR(Status))
      {
        FreePool(Decoded);
        return Status;
      }
      FreePool(*Buffer);
      *Buffer = Decoded;
      *Size = *Capacity = Written;
      *Info = Result;
      return EFI_SUCCESS;
    }
  }
  if (DataSize > MaxDecodedSize)
    return EFI_BAD_BUFFER_SIZE;
  if (Data != *Buffer)
    CopyMem(*Buffer, Data, DataSize);
  *Size = DataSize;
  *Info = Result;
  return EFI_SUCCESS;
}
