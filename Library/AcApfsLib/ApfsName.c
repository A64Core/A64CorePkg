/** @file
  UTF-8 validation, Unicode 9.0 canonical name comparison, and directory hashes.
**/

#include "ApfsInternal.h"
#include "ApfsUnicodeData.h"

#define ARRAY_COUNT(Array) (sizeof(Array) / sizeof((Array)[0]))

STATIC CONST APFS_UNICODE_MAPPING *
FindMapping(CONST APFS_UNICODE_MAPPING *Table, UINTN Count, UINT32 Codepoint)
{
  UINTN Low;
  UINTN Middle;
  Low = 0;
  while (Low < Count)
  {
    Middle = Low + (Count - Low) / 2;
    if (Table[Middle].Codepoint < Codepoint)
      Low = Middle + 1;
    else if (Table[Middle].Codepoint > Codepoint)
      Count = Middle;
    else
      return &Table[Middle];
  }
  return NULL;
}

STATIC UINT8
CombiningClass(UINT32 Codepoint)
{
  UINTN Low;
  UINTN High;
  UINTN Middle;
  Low = 0;
  High = ARRAY_COUNT(UnicodeClasses);
  while (Low < High)
  {
    Middle = Low + (High - Low) / 2;
    if (UnicodeClasses[Middle].Codepoint < Codepoint)
      Low = Middle + 1;
    else if (UnicodeClasses[Middle].Codepoint > Codepoint)
      High = Middle;
    else
      return UnicodeClasses[Middle].Class;
  }
  return 0;
}

STATIC BOOLEAN
Assigned(UINT32 Codepoint)
{
  UINTN Low;
  UINTN High;
  UINTN Middle;
  Low = 0;
  High = ARRAY_COUNT(UnicodeAssigned);
  while (Low < High)
  {
    Middle = Low + (High - Low) / 2;
    if (UnicodeAssigned[Middle].Last < Codepoint)
      Low = Middle + 1;
    else if (UnicodeAssigned[Middle].First > Codepoint)
      High = Middle;
    else
      return TRUE;
  }
  return FALSE;
}

STATIC EFI_STATUS
Decode(CONST UINT8 *Bytes, UINTN Length, UINTN *Offset, UINT32 *Point)
{
  UINT32 Value;
  UINT32 Minimum;
  UINTN  Count;
  UINTN  Index;
  UINT8  First;

  First = Bytes[(*Offset)++];
  if (First < 0x80)
  {
    Value = First;
    Count = 0;
    Minimum = 0;
  }
  else if (First >= 0xC2 && First <= 0xDF)
  {
    Value = First & 0x1FU;
    Count = 1;
    Minimum = 0x80;
  }
  else if (First >= 0xE0 && First <= 0xEF)
  {
    Value = First & 0xFU;
    Count = 2;
    Minimum = 0x800;
  }
  else if (First >= 0xF0 && First <= 0xF4)
  {
    Value = First & 0x7U;
    Count = 3;
    Minimum = 0x10000;
  }
  else
    return EFI_COMPROMISED_DATA;
  if (Count > Length - *Offset)
    return EFI_COMPROMISED_DATA;
  for (Index = 0; Index < Count; ++Index)
  {
    First = Bytes[(*Offset)++];
    if ((First & 0xC0U) != 0x80U)
      return EFI_COMPROMISED_DATA;
    Value = (Value << 6U) | (First & 0x3FU);
  }
  if (Value < Minimum || Value > 0x10FFFF || (Value >= 0xD800 && Value <= 0xDFFF) || Value == 0 ||
      Value == '/' || (Value >= 0xFDD0 && Value <= 0xFDEF) || (Value & 0xFFFFU) >= 0xFFFE)
    return EFI_COMPROMISED_DATA;
  if (!Assigned(Value))
    return EFI_UNSUPPORTED;
  *Point = Value;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
Decompose(UINT32 Point, UINT32 *Output, UINTN *Count, UINTN Capacity)
{
  CONST APFS_UNICODE_MAPPING *Mapping;
  EFI_STATUS                  Status;
  UINTN                       Index;
  UINT32                      Syllable;

  if (Point >= 0xAC00 && Point < 0xD7A4)
  {
    Syllable = Point - 0xAC00;
    if (Capacity - *Count < 3)
      return EFI_COMPROMISED_DATA;
    Output[(*Count)++] = 0x1100 + Syllable / 588;
    Output[(*Count)++] = 0x1161 + (Syllable % 588) / 28;
    if (Syllable % 28 != 0)
      Output[(*Count)++] = 0x11A7 + Syllable % 28;
    return EFI_SUCCESS;
  }
  Mapping = FindMapping(UnicodeDecomposition, ARRAY_COUNT(UnicodeDecomposition), Point);
  if (Mapping != NULL)
  {
    for (Index = 0; Index < Mapping->Count; ++Index)
    {
      Status =
        Decompose(UnicodeDecompositionPoints[Mapping->Offset + Index], Output, Count, Capacity);
      if (EFI_ERROR(Status))
        return Status;
    }
    return EFI_SUCCESS;
  }
  if (*Count == Capacity)
    return EFI_COMPROMISED_DATA;
  Output[(*Count)++] = Point;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
Reorder(UINT32 *Points, UINTN Count)
{
  UINTN  Index;
  UINTN  Position;
  UINTN  Nonstarters;
  UINT32 Point;
  UINT8  Class;

  Nonstarters = 0;
  for (Index = 0; Index < Count; ++Index)
  {
    Point = Points[Index];
    Class = CombiningClass(Point);
    if (Class == 0)
      Nonstarters = 0;
    else if (++Nonstarters > 30)
      return EFI_COMPROMISED_DATA;
    Position = Index;
    while (Class != 0 && Position != 0 && CombiningClass(Points[Position - 1]) > Class)
    {
      Points[Position] = Points[Position - 1];
      --Position;
    }
    Points[Position] = Point;
  }
  return EFI_SUCCESS;
}

VOID
ApfsFreeName(APFS_NAME *Name)
{
  if (Name->Points != NULL)
    FreePool(Name->Points);
  ZeroMem(Name, sizeof(*Name));
}

EFI_STATUS
ApfsNormalizeName(CONST CHAR8 *Bytes, UINTN Length, BOOLEAN Normalize, BOOLEAN Fold,
                  OUT APFS_NAME *Name)
{
  CONST APFS_UNICODE_MAPPING *Mapping;
  UINT32                     *Scratch;
  UINT32                      Point;
  UINT32                      Hash;
  UINTN                       Capacity;
  UINTN                       Offset;
  UINTN                       Count;
  UINTN                       Index;
  UINTN                       Part;
  UINTN                       Byte;
  UINTN                       Bit;
  EFI_STATUS                  Status;

  ZeroMem(Name, sizeof(*Name));
  if (Length == 0 || Length > MAX_UINT16 - 1U)
    return EFI_COMPROMISED_DATA;
  Capacity = Length * 4;
  Name->Points = AllocateZeroPool(Capacity * sizeof(*Name->Points));
  if (Name->Points == NULL)
    return EFI_OUT_OF_RESOURCES;
  Offset = 0;
  while (Offset < Length)
  {
    Status = Decode((CONST UINT8 *)Bytes, Length, &Offset, &Point);
    if (EFI_ERROR(Status))
      goto Failure;
    if (Normalize || Fold)
      Status = Decompose(Point, Name->Points, &Name->Count, Capacity);
    else
    {
      Name->Points[Name->Count++] = Point;
      Status = EFI_SUCCESS;
    }
    if (EFI_ERROR(Status))
      goto Failure;
  }
  if (Normalize || Fold)
  {
    Status = Reorder(Name->Points, Name->Count);
    if (EFI_ERROR(Status))
      goto Failure;
  }
  if (Fold)
  {
    Scratch = AllocateZeroPool(Capacity * sizeof(*Scratch));
    if (Scratch == NULL)
    {
      Status = EFI_OUT_OF_RESOURCES;
      goto Failure;
    }
    Count = 0;
    for (Index = 0; Index < Name->Count; ++Index)
    {
      Point = Name->Points[Index];
      Mapping = FindMapping(UnicodeFolding, ARRAY_COUNT(UnicodeFolding), Point);
      for (Part = 0; Part < (Mapping == NULL ? 1 : Mapping->Count); ++Part)
      {
        Status = Decompose(Mapping == NULL ? Point : UnicodeFoldingPoints[Mapping->Offset + Part],
                           Scratch, &Count, Capacity);
        if (EFI_ERROR(Status))
        {
          FreePool(Scratch);
          goto Failure;
        }
      }
    }
    FreePool(Name->Points);
    Name->Points = Scratch;
    Name->Count = Count;
    Status = Reorder(Name->Points, Name->Count);
    if (EFI_ERROR(Status))
      goto Failure;
  }
  Hash = MAX_UINT32;
  for (Index = 0; Index < Name->Count; ++Index)
    for (Byte = 0; Byte < 4; ++Byte)
    {
      Hash ^= (Name->Points[Index] >> (Byte * 8U)) & 0xFFU;
      for (Bit = 0; Bit < 8; ++Bit)
        Hash = (Hash >> 1U) ^ ((Hash & 1U) != 0 ? 0x82F63B78U : 0);
    }
  Name->Hash = Hash & 0x3FFFFFU;
  return EFI_SUCCESS;

Failure:
  ApfsFreeName(Name);
  return Status;
}

EFI_STATUS
ApfsDirectoryKey(CONST APFS_RECORD *Record, BOOLEAN Hashed, OUT CONST CHAR8 **Name,
                 OUT UINTN *Length, OUT UINT32 *Hash)
{
  UINTN  Prefix;
  UINTN  Size;
  UINT32 Packed;

  Prefix = Hashed ? 12 : 10;
  if (Record->KeySize < Prefix || (ApfsRead64(Record->Key) >> 60U) != APFS_RECORD_DIRECTORY)
    return EFI_COMPROMISED_DATA;
  Packed = Hashed ? ApfsRead32(Record->Key + 8) : ApfsRead16(Record->Key + 8);
  Size = Hashed ? Packed & APFS_NAME_LENGTH_MASK : Packed;
  if (Size < 2 || Size != Record->KeySize - Prefix || Record->Key[Record->KeySize - 1] != 0)
    return EFI_COMPROMISED_DATA;
  *Name = (CONST CHAR8 *)Record->Key + Prefix;
  *Length = Size - 1;
  *Hash = Hashed ? Packed >> 10U : 0;
  return EFI_SUCCESS;
}
