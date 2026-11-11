/** @file
  IMG4 container and IM4P component extraction.
**/

#include "Img4Internal.h"

STATIC EFI_STATUS
ReadMagic(AC_DER_CURSOR *Cursor, CONST CHAR8 Magic[4])
{
  AC_DER_ITEM Item;
  EFI_STATUS  Status;

  Status = InternalDerRead(Cursor, &Item);
  if (EFI_ERROR(Status))
    return Status;
  if ((Item.Tag != 0x16) || (Item.Size != 4) || (CompareMem(Item.Data, Magic, 4) != 0))
    return EFI_COMPROMISED_DATA;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ValidateWrapped(CONST AC_DER_ITEM *Item, CONST CHAR8 Magic[4])
{
  AC_DER_CURSOR Cursor;
  AC_DER_CURSOR Body;
  AC_DER_ITEM   Sequence;
  EFI_STATUS    Status;
  UINTN         Count;

  Cursor.Data = Item->Data;
  Cursor.Size = Item->Size;
  Status = InternalDerRead(&Cursor, &Sequence);
  if (EFI_ERROR(Status) || (Sequence.Tag != 0x30) || (Cursor.Size != 0))
    return EFI_COMPROMISED_DATA;
  Body.Data = Sequence.Data;
  Body.Size = Sequence.Size;
  Status = ReadMagic(&Body, Magic);
  if (EFI_ERROR(Status))
    return Status;
  Count = 0;
  return InternalDerValidate(Body, 1, &Count);
}

STATIC EFI_STATUS
ParseIm4p(AC_DER_CURSOR Cursor, AC_IMG4_PAYLOAD *Payload)
{
  AC_DER_ITEM   Item;
  AC_DER_ITEM   Integer;
  AC_DER_CURSOR Compression;
  EFI_STATUS    Status;
  UINTN         Index;
  UINTN         Order;
  UINTN         Previous;

  Status = ReadMagic(&Cursor, "IM4P");
  if (EFI_ERROR(Status))
    return Status;
  Status = InternalDerRead(&Cursor, &Item);
  if (EFI_ERROR(Status) || (Item.Tag != 0x16) || (Item.Size != 4))
    return EFI_COMPROMISED_DATA;
  for (Index = 0; Index < 4; ++Index)
    if ((Item.Data[Index] < 0x20) || (Item.Data[Index] > 0x7E))
      return EFI_COMPROMISED_DATA;
  CopyMem(Payload->ComponentType, Item.Data, 4);
  Status = InternalDerRead(&Cursor, &Item);
  if (EFI_ERROR(Status) || (Item.Tag != 0x16))
    return EFI_COMPROMISED_DATA;
  for (Index = 0; Index < Item.Size; ++Index)
    if (Item.Data[Index] > 0x7F)
      return EFI_COMPROMISED_DATA;
  Status = InternalDerRead(&Cursor, &Item);
  if (EFI_ERROR(Status) || (Item.Tag != 4))
    return EFI_COMPROMISED_DATA;
  Payload->Data = Item.Data;
  Payload->DataSize = Item.Size;
  Previous = 0;
  while (Cursor.Size != 0)
  {
    Status = InternalDerRead(&Cursor, &Item);
    if (EFI_ERROR(Status))
      return Status;
    if (Item.Tag == 0x30)
    {
      Order = 1;
      Compression.Data = Item.Data;
      Compression.Size = Item.Size;
      Status = InternalDerRead(&Compression, &Integer);
      if (!EFI_ERROR(Status))
        Status = InternalDerInteger(&Integer, &Payload->CompressionAlgorithm);
      if (!EFI_ERROR(Status))
        Status = InternalDerRead(&Compression, &Integer);
      if (!EFI_ERROR(Status))
        Status = InternalDerInteger(&Integer, &Payload->UncompressedSize);
      if (EFI_ERROR(Status) || (Compression.Size != 0))
        return EFI_COMPROMISED_DATA;
      Payload->HasCompression = TRUE;
    }
    else if (Item.Tag == 4)
    {
      if (Previous < 2)
      {
        Order = 2;
        Payload->HasKeybag = Item.Size != 0;
      }
      else
        Order = 4;
    }
    else if (Item.Tag == 0xA0)
    {
      Order = 3;
      Status = ValidateWrapped(&Item, "PAYP");
      if (EFI_ERROR(Status))
        return Status;
    }
    else
      return EFI_UNSUPPORTED;
    if (Order <= Previous)
      return EFI_COMPROMISED_DATA;
    Previous = Order;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
AcImg4Parse(IN CONST VOID *Buffer, IN UINTN BufferSize, OUT AC_IMG4_PAYLOAD *Payload)
{
  AC_IMG4_PAYLOAD Result;
  AC_DER_CURSOR   Cursor;
  AC_DER_CURSOR   Body;
  AC_DER_ITEM     Sequence;
  AC_DER_ITEM     Magic;
  AC_DER_ITEM     Item;
  EFI_STATUS      Status;
  UINT8           Previous;

  if (Payload == NULL)
    return EFI_INVALID_PARAMETER;
  ZeroMem(Payload, sizeof(*Payload));
  if (Buffer == NULL)
    return EFI_INVALID_PARAMETER;
  if ((BufferSize == 0) || (((CONST UINT8 *)Buffer)[0] != 0x30))
    return EFI_UNSUPPORTED;
  ZeroMem(&Result, sizeof(Result));
  Cursor.Data = Buffer;
  Cursor.Size = BufferSize;
  Status = InternalDerRead(&Cursor, &Sequence);
  if (EFI_ERROR(Status) || (Cursor.Size != 0))
    return EFI_COMPROMISED_DATA;
  Body.Data = Sequence.Data;
  Body.Size = Sequence.Size;
  Cursor = Body;
  Status = InternalDerRead(&Cursor, &Magic);
  if (EFI_ERROR(Status) || (Magic.Tag != 0x16) || (Magic.Size != 4))
    return EFI_COMPROMISED_DATA;
  if (CompareMem(Magic.Data, "IM4P", 4) == 0)
  {
    Result.Container = AcImg4ContainerIm4p;
    Status = ParseIm4p(Body, &Result);
  }
  else if (CompareMem(Magic.Data, "IMG4", 4) == 0)
  {
    Result.Container = AcImg4ContainerImg4;
    Status = InternalDerRead(&Cursor, &Item);
    if (EFI_ERROR(Status) || (Item.Tag != 0x30))
      return EFI_COMPROMISED_DATA;
    Body.Data = Item.Data;
    Body.Size = Item.Size;
    Status = ParseIm4p(Body, &Result);
    Previous = 0;
    while (!EFI_ERROR(Status) && (Cursor.Size != 0))
    {
      Status = InternalDerRead(&Cursor, &Item);
      if (EFI_ERROR(Status))
        return Status;
      if ((Item.Tag != 0xA0) && (Item.Tag != 0xA1))
        return EFI_UNSUPPORTED;
      if (Previous >= Item.Tag)
        return EFI_COMPROMISED_DATA;
      Previous = Item.Tag;
      Status = ValidateWrapped(&Item, Item.Tag == 0xA0 ? "IM4M" : "IM4R");
    }
  }
  else
    return EFI_UNSUPPORTED;
  if (!EFI_ERROR(Status))
    *Payload = Result;
  return Status;
}

STATIC EFI_STATUS
ValidateManifest(CONST UINT8 *Data, UINTN Size)
{
  AC_DER_ITEM   Wrapped = {0, Data, Size};
  AC_DER_ITEM   Item;
  AC_DER_CURSOR Cursor = {Data, Size};
  AC_DER_CURSOR Body;
  UINT64        Version;
  EFI_STATUS    Status;

  Status = ValidateWrapped(&Wrapped, "IM4M");
  if (EFI_ERROR(Status))
    return Status;
  Status = InternalDerRead(&Cursor, &Item);
  if (EFI_ERROR(Status))
    return Status;
  Body.Data = Item.Data;
  Body.Size = Item.Size;
  Status = ReadMagic(&Body, "IM4M");
  if (!EFI_ERROR(Status))
    Status = InternalDerRead(&Body, &Item);
  if (!EFI_ERROR(Status))
    Status = InternalDerInteger(&Item, &Version);
  if (EFI_ERROR(Status))
    return Status;
  if (Version != 0)
    return EFI_UNSUPPORTED;
  Status = InternalDerRead(&Body, &Item);
  if (EFI_ERROR(Status) || Item.Tag != 0x31 || Item.Size == 0)
    return EFI_COMPROMISED_DATA;
  Status = InternalDerRead(&Body, &Item);
  if (EFI_ERROR(Status) || Item.Tag != 4 || Item.Size == 0)
    return EFI_COMPROMISED_DATA;
  Status = InternalDerRead(&Body, &Item);
  if (EFI_ERROR(Status) || Item.Tag != 0x30 || Item.Size == 0 || Body.Size != 0)
    return EFI_COMPROMISED_DATA;
  return EFI_SUCCESS;
}

EFI_STATUS
AcImg4GetManifest(IN CONST VOID *Buffer, IN UINTN BufferSize, OUT AC_IMG4_MANIFEST *Manifest)
{
  AC_DER_CURSOR    Cursor;
  AC_DER_ITEM      Sequence;
  AC_DER_ITEM      Magic;
  AC_DER_ITEM      Item;
  AC_IMG4_PAYLOAD  Payload;
  AC_IMG4_MANIFEST Result = {0};
  EFI_STATUS       Status;

  if (Manifest == NULL)
    return EFI_INVALID_PARAMETER;
  ZeroMem(Manifest, sizeof(*Manifest));
  if (Buffer == NULL)
    return EFI_INVALID_PARAMETER;
  if (BufferSize == 0 || ((CONST UINT8 *)Buffer)[0] != 0x30)
    return EFI_UNSUPPORTED;
  Cursor.Data = Buffer;
  Cursor.Size = BufferSize;
  Status = InternalDerRead(&Cursor, &Sequence);
  if (EFI_ERROR(Status) || Cursor.Size != 0)
    return EFI_COMPROMISED_DATA;
  Cursor.Data = Sequence.Data;
  Cursor.Size = Sequence.Size;
  Status = InternalDerRead(&Cursor, &Magic);
  if (EFI_ERROR(Status) || Magic.Tag != 0x16 || Magic.Size != 4)
    return EFI_COMPROMISED_DATA;
  if (CompareMem(Magic.Data, "IM4M", 4) == 0)
  {
    Result.Data = Buffer;
    Result.Size = BufferSize;
  }
  else
  {
    Status = AcImg4Parse(Buffer, BufferSize, &Payload);
    if (EFI_ERROR(Status))
      return Status;
    if (Payload.Container != AcImg4ContainerImg4)
      return EFI_NOT_FOUND;
    Status = InternalDerRead(&Cursor, &Item);
    if (EFI_ERROR(Status))
      return Status;
    while (Cursor.Size != 0)
    {
      Status = InternalDerRead(&Cursor, &Item);
      if (EFI_ERROR(Status))
        return Status;
      if (Item.Tag == 0xA0)
      {
        Result.Data = Item.Data;
        Result.Size = Item.Size;
      }
    }
    if (Result.Data == NULL)
      return EFI_NOT_FOUND;
  }
  Status = ValidateManifest(Result.Data, Result.Size);
  if (!EFI_ERROR(Status))
    *Manifest = Result;
  return Status;
}
