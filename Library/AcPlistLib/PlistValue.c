/** @file
  Unsigned integer conversion and Base64 property-list data decoding.
**/

#include "PlistInternal.h"

STATIC BOOLEAN
IsSpace(CHAR8 Character)
{
  return Character == ' ' || Character == '\t' || Character == '\r' || Character == '\n';
}

EFI_STATUS
AcPlistGetUnsignedInteger(IN CONST AC_PLIST_NODE *Node, OUT UINT64 *Value)
{
  CONST CHAR8 *Text;
  UINT64       Result = 0;
  UINT32       Base = 10;
  UINT32       Digit;
  BOOLEAN      HasDigit = FALSE;

  if (Value == NULL)
    return EFI_INVALID_PARAMETER;
  *Value = 0;
  if (Node == NULL || Node->Type != AcPlistTypeInteger)
    return EFI_INVALID_PARAMETER;
  Text = Node->Text;
  while (IsSpace(*Text))
    ++Text;
  if (*Text == '+')
    ++Text;
  if (Text[0] == '0' && (Text[1] == 'x' || Text[1] == 'X'))
  {
    Base = 16;
    Text += 2;
  }
  for (; *Text != '\0' && !IsSpace(*Text); ++Text)
  {
    if (*Text >= '0' && *Text <= '9')
      Digit = (UINT32)(*Text - '0');
    else if (*Text >= 'a' && *Text <= 'f')
      Digit = (UINT32)(*Text - 'a' + 10);
    else if (*Text >= 'A' && *Text <= 'F')
      Digit = (UINT32)(*Text - 'A' + 10);
    else
      return EFI_COMPROMISED_DATA;
    if (Digit >= Base)
      return EFI_COMPROMISED_DATA;
    if (Result > (MAX_UINT64 - Digit) / Base)
      return EFI_BAD_BUFFER_SIZE;
    Result = Result * Base + Digit;
    HasDigit = TRUE;
  }
  while (IsSpace(*Text))
    ++Text;
  if (!HasDigit || *Text != '\0')
    return EFI_COMPROMISED_DATA;
  *Value = Result;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
DecodeBase64(CONST CHAR8 *Text, UINT8 *Buffer OPTIONAL, OUT UINTN *Size)
{
  UINT8   Group[4];
  UINT8   Value;
  UINTN   Used = 0;
  UINTN   Count = 0;
  UINTN   Bytes;
  BOOLEAN Padded = FALSE;

  *Size = 0;
  for (; *Text != '\0'; ++Text)
  {
    if (IsSpace(*Text))
      continue;
    if (Padded)
      return EFI_COMPROMISED_DATA;
    if (*Text >= 'A' && *Text <= 'Z')
      Value = (UINT8)(*Text - 'A');
    else if (*Text >= 'a' && *Text <= 'z')
      Value = (UINT8)(*Text - 'a' + 26);
    else if (*Text >= '0' && *Text <= '9')
      Value = (UINT8)(*Text - '0' + 52);
    else if (*Text == '+')
      Value = 62;
    else if (*Text == '/')
      Value = 63;
    else if (*Text == '=')
      Value = 64;
    else
      return EFI_COMPROMISED_DATA;
    Group[Used++] = Value;
    if (Used != ARRAY_SIZE(Group))
      continue;
    if (Group[0] == 64 || Group[1] == 64 ||
        (Group[2] == 64 && (Group[3] != 64 || (Group[1] & 15U) != 0)) ||
        (Group[2] != 64 && Group[3] == 64 && (Group[2] & 3U) != 0))
      return EFI_COMPROMISED_DATA;
    Bytes = Group[2] == 64 ? 1 : Group[3] == 64 ? 2 : 3;
    if (Count > MAX_UINTN - Bytes)
      return EFI_BAD_BUFFER_SIZE;
    if (Buffer != NULL)
    {
      Buffer[Count] = (UINT8)((Group[0] << 2) | (Group[1] >> 4));
      if (Bytes >= 2)
        Buffer[Count + 1] = (UINT8)((Group[1] << 4) | (Group[2] >> 2));
      if (Bytes == 3)
        Buffer[Count + 2] = (UINT8)((Group[2] << 6) | Group[3]);
    }
    Count += Bytes;
    Used = 0;
    Padded = Bytes != 3;
  }
  if (Used != 0)
    return EFI_COMPROMISED_DATA;
  *Size = Count;
  return EFI_SUCCESS;
}

EFI_STATUS
AcPlistDecodeData(IN CONST AC_PLIST_NODE *Node, OUT UINT8 **Buffer, OUT UINTN *Size)
{
  EFI_STATUS Status;
  UINTN      Length;
  UINT8     *Decoded;

  if (Buffer != NULL)
    *Buffer = NULL;
  if (Size != NULL)
    *Size = 0;
  if (Buffer == NULL || Size == NULL || Node == NULL || Node->Type != AcPlistTypeData)
    return EFI_INVALID_PARAMETER;
  Status = DecodeBase64(Node->Text, NULL, &Length);
  if (EFI_ERROR(Status) || Length == 0)
    return Status;
  Decoded = AllocateZeroPool(Length);
  if (Decoded == NULL)
    return EFI_OUT_OF_RESOURCES;
  Status = DecodeBase64(Node->Text, Decoded, Size);
  if (EFI_ERROR(Status))
  {
    FreePool(Decoded);
    return Status;
  }
  *Buffer = Decoded;
  return EFI_SUCCESS;
}
