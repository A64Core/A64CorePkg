/** @file
  Bounded physical object reads and Fletcher checksum verification.
**/

#include "ApfsInternal.h"

UINT16
ApfsRead16(CONST VOID *Buffer)
{
  UINT16 Value;
  CopyMem(&Value, Buffer, sizeof(Value));
  return Value;
}

UINT32
ApfsRead32(CONST VOID *Buffer)
{
  UINT32 Value;
  CopyMem(&Value, Buffer, sizeof(Value));
  return Value;
}

UINT64
ApfsRead64(CONST VOID *Buffer)
{
  UINT64 Value;
  CopyMem(&Value, Buffer, sizeof(Value));
  return Value;
}

EFI_STATUS
ApfsReadBytes(CONST AC_APFS_CONTAINER *Container, UINT64 Offset, UINTN Size, VOID *Buffer)
{
  UINT64 End;

  End = Container->Superblock.BlockCount * Container->Superblock.BlockSize;
  if ((Offset > End) || (Size > End - Offset) || (Offset > Container->Io.Size) ||
      (Size > Container->Io.Size - Offset))
    return EFI_COMPROMISED_DATA;
  if (Size == 0)
    return EFI_SUCCESS;
  return Container->Io.Read(Container->Io.Context, Offset, Size, Buffer);
}

BOOLEAN
ApfsObjectValid(IN CONST VOID *Buffer, IN UINT32 Size)
{
  APFS_OBJECT Header;
  UINT64      Sum1;
  UINT64      Sum2;
  UINT64      Check1;
  UINT64      Check2;
  UINT32      Word;
  UINT32      Offset;

  if ((Size < sizeof(Header)) || ((Size & 3U) != 0))
    return FALSE;

  CopyMem(&Header, Buffer, sizeof(Header));
  if ((Header.Oid == 0) || (Header.Xid == 0))
    return FALSE;

  Sum1 = 0;
  Sum2 = 0;
  for (Offset = sizeof(Header.Checksum); Offset < Size; Offset += sizeof(Word))
  {
    CopyMem(&Word, (CONST UINT8 *)Buffer + Offset, sizeof(Word));
    Sum1 = (Sum1 + Word) % MAX_UINT32;
    Sum2 = (Sum2 + Sum1) % MAX_UINT32;
  }
  Check1 = MAX_UINT32 - ((Sum1 + Sum2) % MAX_UINT32);
  Check2 = MAX_UINT32 - ((Sum1 + Check1) % MAX_UINT32);
  return Header.Checksum == ((Check2 << 32U) | Check1);
}

EFI_STATUS
ApfsReadObject(IN CONST AC_APFS_CONTAINER *Container, IN UINT64 Address, IN UINT32 Size,
               OUT VOID **Buffer, OUT BOOLEAN *ReadFailed OPTIONAL)
{
  EFI_STATUS Status;
  UINT64     Offset;
  UINT32     BlockSize;

  *Buffer = NULL;
  if (ReadFailed != NULL)
    *ReadFailed = FALSE;
  BlockSize = Container->Superblock.BlockSize;
  if ((Size == 0) || ((Size % BlockSize) != 0) || (Address >= Container->Superblock.BlockCount) ||
      (Size / BlockSize > Container->Superblock.BlockCount - Address))
    return EFI_COMPROMISED_DATA;

  Offset = Address * BlockSize;
  if ((Offset > Container->Io.Size) || (Size > Container->Io.Size - Offset))
    return EFI_COMPROMISED_DATA;

  *Buffer = AllocateZeroPool(Size);
  if (*Buffer == NULL)
    return EFI_OUT_OF_RESOURCES;

  Status = Container->Io.Read(Container->Io.Context, Offset, Size, *Buffer);
  if (ReadFailed != NULL)
    *ReadFailed = EFI_ERROR(Status);
  if (!EFI_ERROR(Status) && !ApfsObjectValid(*Buffer, Size))
    Status = EFI_COMPROMISED_DATA;
  if (EFI_ERROR(Status))
  {
    FreePool(*Buffer);
    *Buffer = NULL;
  }
  return Status;
}
