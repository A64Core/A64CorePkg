/** @file
  Definite-length DER framing and unsigned integer decoding.
**/

#include "Img4Internal.h"

EFI_STATUS
InternalDerRead(IN OUT AC_DER_CURSOR *Cursor, OUT AC_DER_ITEM *Item)
{
  UINTN  Offset;
  UINTN  Length;
  UINTN  Count;
  UINTN  Index;
  UINT64 TagNumber;
  UINT8  Byte;

  if (Cursor->Size < 2)
    return EFI_COMPROMISED_DATA;
  Offset = 1;
  Item->Tag = Cursor->Data[0];
  if ((Item->Tag & 0x1FU) == 0x1FU)
  {
    TagNumber = 0;
    if (Cursor->Data[Offset] == 0x80)
      return EFI_COMPROMISED_DATA;
    do
    {
      if ((Offset == Cursor->Size) || (TagNumber > (MAX_UINT64 >> 7)))
        return EFI_COMPROMISED_DATA;
      Byte = Cursor->Data[Offset++];
      TagNumber = (TagNumber << 7) | (Byte & 0x7FU);
    } while ((Byte & 0x80U) != 0);
    if (TagNumber < 31)
      return EFI_COMPROMISED_DATA;
  }
  if ((Item->Tag & 0xC0U) == 0 && (Item->Tag & 0x1FU) == 0)
    return EFI_COMPROMISED_DATA;
  if (Offset == Cursor->Size)
    return EFI_COMPROMISED_DATA;
  Byte = Cursor->Data[Offset++];
  Length = Byte;
  if ((Byte & 0x80U) != 0)
  {
    Count = Byte & 0x7FU;
    if ((Count == 0) || (Count > sizeof(UINTN)) || (Count > Cursor->Size - Offset) ||
        (Cursor->Data[Offset] == 0))
      return EFI_COMPROMISED_DATA;
    Length = 0;
    for (Index = 0; Index < Count; ++Index)
      Length = (Length << 8) | Cursor->Data[Offset++];
    if (Length < 128)
      return EFI_COMPROMISED_DATA;
  }
  if (Length > Cursor->Size - Offset)
    return EFI_COMPROMISED_DATA;
  Item->Data = Cursor->Data + Offset;
  Item->Size = Length;
  Cursor->Data += Offset + Length;
  Cursor->Size -= Offset + Length;
  return EFI_SUCCESS;
}

EFI_STATUS
InternalDerInteger(IN CONST AC_DER_ITEM *Item, OUT UINT64 *Value)
{
  UINTN Index;
  UINTN Start;

  *Value = 0;
  if ((Item->Tag != 2) || (Item->Size == 0) || (Item->Size > 9) || ((Item->Data[0] & 0x80U) != 0))
    return EFI_COMPROMISED_DATA;
  Start = 0;
  if ((Item->Size > 1) && (Item->Data[0] == 0))
  {
    if ((Item->Data[1] & 0x80U) == 0)
      return EFI_COMPROMISED_DATA;
    Start = 1;
  }
  if (Item->Size - Start > sizeof(*Value))
    return EFI_COMPROMISED_DATA;
  for (Index = Start; Index < Item->Size; ++Index)
    *Value = (*Value << 8) | Item->Data[Index];
  return EFI_SUCCESS;
}

EFI_STATUS
InternalDerValidate(IN AC_DER_CURSOR Cursor, IN UINTN Depth, IN OUT UINTN *Count)
{
  AC_DER_ITEM   Item;
  AC_DER_CURSOR Child;
  EFI_STATUS    Status;

  if (Depth > 16)
    return EFI_BAD_BUFFER_SIZE;
  while (Cursor.Size != 0)
  {
    if (++*Count > 65536)
      return EFI_BAD_BUFFER_SIZE;
    Status = InternalDerRead(&Cursor, &Item);
    if (EFI_ERROR(Status))
      return Status;
    if ((Item.Tag & 0x20U) != 0)
    {
      Child.Data = Item.Data;
      Child.Size = Item.Size;
      Status = InternalDerValidate(Child, Depth + 1, Count);
      if (EFI_ERROR(Status))
        return Status;
    }
  }
  return EFI_SUCCESS;
}
