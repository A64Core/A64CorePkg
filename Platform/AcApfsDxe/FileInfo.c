/** @file
  UTF conversion and standard read-only file information.
**/

#include "ApfsDxe.h"

EFI_STATUS
ApfsDxeUtf8ToUnicode(CONST CHAR8 *Text, OUT CHAR16 **Result)
{
  CONST UINT8 *Bytes = (CONST UINT8 *)Text;
  CHAR16      *Unicode;
  UINTN        Length;
  UINTN        Index;
  UINTN        Count;
  UINTN        Tail;
  UINT32       Point;
  UINT32       Minimum;

  *Result = NULL;
  Length = AsciiStrLen(Text);
  if (Length > MAX_UINTN / sizeof(CHAR16) - 1)
    return EFI_OUT_OF_RESOURCES;
  Unicode = AllocateZeroPool((Length + 1) * sizeof(CHAR16));
  if (Unicode == NULL)
    return EFI_OUT_OF_RESOURCES;
  Count = 0;
  for (Index = 0; Index < Length;)
  {
    Point = Bytes[Index++];
    Tail = 0;
    Minimum = 0;
    if (Point >= 0xC2 && Point <= 0xDF)
    {
      Point &= 0x1F;
      Tail = 1;
      Minimum = 0x80;
    }
    else if (Point >= 0xE0 && Point <= 0xEF)
    {
      Point &= 0xF;
      Tail = 2;
      Minimum = 0x800;
    }
    else if (Point >= 0xF0 && Point <= 0xF4)
    {
      Point &= 7;
      Tail = 3;
      Minimum = 0x10000;
    }
    else if (Point >= 0x80)
      goto Invalid;
    if (Tail > Length - Index)
      goto Invalid;
    while (Tail-- != 0)
    {
      if ((Bytes[Index] & 0xC0) != 0x80)
        goto Invalid;
      Point = (Point << 6) | (Bytes[Index++] & 0x3F);
    }
    if (Point < Minimum || Point > 0x10FFFF || (Point >= 0xD800 && Point <= 0xDFFF))
      goto Invalid;
    if (Point >= 0x10000)
    {
      Point -= 0x10000;
      Unicode[Count++] = (CHAR16)(0xD800 | (Point >> 10));
      Unicode[Count++] = (CHAR16)(0xDC00 | (Point & 0x3FF));
    }
    else
      Unicode[Count++] = (CHAR16)Point;
  }
  *Result = Unicode;
  return EFI_SUCCESS;

Invalid:
  FreePool(Unicode);
  return EFI_VOLUME_CORRUPTED;
}

EFI_STATUS
ApfsDxeUnicodeToPath(CONST CHAR16 *Text, OUT CHAR8 **Path)
{
  CHAR8 *Bytes;
  UINTN  Length;
  UINTN  Index;
  UINTN  Count;
  UINT32 Point;

  *Path = NULL;
  Length = StrLen(Text);
  if (Length > (MAX_UINTN - 1) / 3)
    return EFI_OUT_OF_RESOURCES;
  Bytes = AllocateZeroPool(Length * 3 + 1);
  if (Bytes == NULL)
    return EFI_OUT_OF_RESOURCES;
  Count = 0;
  for (Index = 0; Index < Length; ++Index)
  {
    Point = Text[Index];
    if (Point >= 0xD800 && Point <= 0xDBFF)
    {
      if (++Index == Length || Text[Index] < 0xDC00 || Text[Index] > 0xDFFF)
        goto Invalid;
      Point = 0x10000 + ((Point - 0xD800) << 10) + Text[Index] - 0xDC00;
    }
    else if (Point >= 0xDC00 && Point <= 0xDFFF)
      goto Invalid;
    if (Point == '\\')
      Point = '/';
    if (Point < 0x80)
      Bytes[Count++] = (CHAR8)Point;
    else if (Point < 0x800)
    {
      Bytes[Count++] = (CHAR8)(0xC0 | (Point >> 6));
      Bytes[Count++] = (CHAR8)(0x80 | (Point & 0x3F));
    }
    else if (Point < 0x10000)
    {
      Bytes[Count++] = (CHAR8)(0xE0 | (Point >> 12));
      Bytes[Count++] = (CHAR8)(0x80 | ((Point >> 6) & 0x3F));
      Bytes[Count++] = (CHAR8)(0x80 | (Point & 0x3F));
    }
    else
    {
      Bytes[Count++] = (CHAR8)(0xF0 | (Point >> 18));
      Bytes[Count++] = (CHAR8)(0x80 | ((Point >> 12) & 0x3F));
      Bytes[Count++] = (CHAR8)(0x80 | ((Point >> 6) & 0x3F));
      Bytes[Count++] = (CHAR8)(0x80 | (Point & 0x3F));
    }
  }
  *Path = Bytes;
  return EFI_SUCCESS;

Invalid:
  FreePool(Bytes);
  return EFI_INVALID_PARAMETER;
}

STATIC BOOLEAN
LeapYear(UINT16 Year)
{
  return Year % 4 == 0 && (Year % 100 != 0 || Year % 400 == 0);
}

STATIC VOID
ConvertTime(UINT64 Nanoseconds, OUT EFI_TIME *Time)
{
  STATIC CONST UINT8 MonthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  UINT64             Seconds = Nanoseconds / 1000000000ULL;
  UINT64             Days = Seconds / 86400;
  UINTN              Length;

  ZeroMem(Time, sizeof(*Time));
  Time->Year = 1970;
  for (;;)
  {
    Length = LeapYear(Time->Year) ? 366 : 365;
    if (Days < Length)
      break;
    Days -= Length;
    ++Time->Year;
  }
  Time->Month = 1;
  for (;;)
  {
    Length = MonthDays[Time->Month - 1] + (Time->Month == 2 && LeapYear(Time->Year) ? 1 : 0);
    if (Days < Length)
      break;
    Days -= Length;
    ++Time->Month;
  }
  Time->Day = (UINT8)(Days + 1);
  Time->Hour = (UINT8)(Seconds / 3600 % 24);
  Time->Minute = (UINT8)(Seconds / 60 % 60);
  Time->Second = (UINT8)(Seconds % 60);
  Time->Nanosecond = (UINT32)(Nanoseconds % 1000000000ULL);
}

EFI_STATUS
ApfsDxeFillFileInfo(CONST AC_APFS_FILE_INFO *Info, CONST CHAR8 *Name, UINTN *BufferSize,
                    VOID *Buffer)
{
  CHAR16        *Unicode;
  EFI_FILE_INFO *Result;
  UINTN          Required;
  UINTN          Capacity = *BufferSize;
  EFI_STATUS     Status;

  *BufferSize = 0;
  Status = ApfsDxeUtf8ToUnicode(Name, &Unicode);
  if (EFI_ERROR(Status))
    return Status;
  Required = SIZE_OF_EFI_FILE_INFO + StrSize(Unicode);
  *BufferSize = Required;
  if (Capacity < Required)
    Status = EFI_BUFFER_TOO_SMALL;
  else if (Buffer == NULL)
    Status = EFI_INVALID_PARAMETER;
  else
  {
    ZeroMem(Buffer, Required);
    Result = Buffer;
    Result->Size = Required;
    Result->FileSize = Info->Size;
    Result->PhysicalSize = Info->AllocatedSize;
    Result->Attribute = EFI_FILE_READ_ONLY;
    if (Info->Type == AcApfsFileDirectory)
      Result->Attribute |= EFI_FILE_DIRECTORY;
    if ((Info->BsdFlags & APFS_BSD_HIDDEN) != 0)
      Result->Attribute |= EFI_FILE_HIDDEN;
    ConvertTime(Info->CreateTime, &Result->CreateTime);
    ConvertTime(Info->AccessTime, &Result->LastAccessTime);
    ConvertTime(Info->ModificationTime, &Result->ModificationTime);
    CopyMem(Result->FileName, Unicode, StrSize(Unicode));
    Status = EFI_SUCCESS;
  }
  FreePool(Unicode);
  return Status;
}

EFI_STATUS EFIAPI
ApfsDxeGetInfo(EFI_FILE_PROTOCOL *This, EFI_GUID *InformationType, UINTN *BufferSize, VOID *Buffer)
{
  APFS_DXE_FILE        *File;
  APFS_DXE_CONTAINER   *Container;
  CONST CHAR8          *Name;
  EFI_FILE_SYSTEM_INFO *System;
  UINTN                 Capacity;
  UINTN                 Required;
  UINTN                 Index;
  EFI_STATUS            Status;

  if (BufferSize == NULL)
    return EFI_INVALID_PARAMETER;
  Capacity = *BufferSize;
  *BufferSize = 0;
  if (This == NULL || InformationType == NULL)
    return EFI_INVALID_PARAMETER;
  File = BASE_CR(This, APFS_DXE_FILE, Protocol);
  Container = File->Volume->Container;
  Status = ApfsDxeCheckMedia(Container);
  if (EFI_ERROR(Status))
    return Status;
  if (CompareGuid(InformationType, &gEfiFileInfoGuid))
  {
    Name = File->Path + 1;
    for (Index = 1; File->Path[Index] != '\0'; ++Index)
      if (File->Path[Index] == '/')
        Name = File->Path + Index + 1;
    *BufferSize = Capacity;
    return ApfsDxeFillFileInfo(&File->Info, Name, BufferSize, Buffer);
  }
  Required = StrSize(File->Volume->Label);
  if (CompareGuid(InformationType, &gEfiFileSystemInfoGuid))
    Required += SIZE_OF_EFI_FILE_SYSTEM_INFO;
  else if (!CompareGuid(InformationType, &gEfiFileSystemVolumeLabelInfoIdGuid))
    return EFI_UNSUPPORTED;
  *BufferSize = Required;
  if (Capacity < Required)
    return EFI_BUFFER_TOO_SMALL;
  if (Buffer == NULL)
    return EFI_INVALID_PARAMETER;
  ZeroMem(Buffer, Required);
  if (CompareGuid(InformationType, &gEfiFileSystemInfoGuid))
  {
    System = Buffer;
    System->Size = Required;
    System->ReadOnly = TRUE;
    System->VolumeSize = Container->Info.BlockCount * Container->Info.BlockSize;
    System->BlockSize = Container->Info.BlockSize;
    // FreeSpace is writable space in this read-only view, not APFS allocation accounting.
    System->FreeSpace = 0;
    CopyMem(System->VolumeLabel, File->Volume->Label, StrSize(File->Volume->Label));
  }
  else
    CopyMem(Buffer, File->Volume->Label, Required);
  return EFI_SUCCESS;
}
