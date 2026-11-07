/** @file
  Bounded Darwin version extraction from a kernel collection's kernel image.
**/

#include "AppleKernelInternal.h"

STATIC
EFI_STATUS
ParseVersion(IN CONST UINT8 *Buffer, IN UINTN BufferSize, OUT UINT32 *Version)
{
  UINTN  Offset;
  UINT32 Part;
  UINT32 Index;
  UINT32 Result;
  UINT32 Digits;

  Offset = 0;
  Result = 0;
  for (Index = 0; Index < 3; ++Index)
  {
    Part = 0;
    Digits = 0;
    while ((Offset < BufferSize) && (Buffer[Offset] >= '0') && (Buffer[Offset] <= '9'))
    {
      if (++Digits > 2)
        return EFI_COMPROMISED_DATA;

      Part = Part * 10 + Buffer[Offset++] - '0';
    }

    if ((Digits == 0) || ((Index == 0) && (Part == 0)))
      return EFI_COMPROMISED_DATA;

    Result = Result * 100 + Part;
    if (Index < 2)
    {
      if ((Offset == BufferSize) || (Buffer[Offset] != '.'))
        return EFI_COMPROMISED_DATA;

      ++Offset;
    }
  }

  // XNU version strings may append a development, alpha, beta, or release suffix.
  if ((Offset < BufferSize) && ((Buffer[Offset] == 'd') || (Buffer[Offset] == 'a') ||
                                (Buffer[Offset] == 'b') || (Buffer[Offset] == 'r')))
  {
    ++Offset;
    Digits = 0;
    while ((Offset < BufferSize) && (Buffer[Offset] >= '0') && (Buffer[Offset] <= '9'))
    {
      ++Offset;
      ++Digits;
    }

    if (Digits == 0)
      return EFI_COMPROMISED_DATA;
  }

  if ((Offset == BufferSize) || (Buffer[Offset] != ':'))
    return EFI_COMPROMISED_DATA;

  *Version = Result;
  return EFI_SUCCESS;
}

EFI_STATUS
AcKernelReadDarwinVersion(IN CONST AC_KERNEL_COLLECTION_CONTEXT *Context, OUT UINT32 *DarwinVersion)
{
  STATIC CONST CHAR8 Prefix[] = "Darwin Kernel Version ";
  EFI_STATUS         Status;
  AC_MACH_HEADER     Header;
  AC_MACH_COMMAND    Command;
  AC_MACH_SEGMENT    Segment;
  UINTN              Offset;
  UINTN              End;
  UINTN              Index;
  UINTN              FileSize;
  CONST UINT8       *Bytes;
  UINT32             Version;
  UINT32             FoundVersion;

  if (DarwinVersion == NULL)
    return EFI_INVALID_PARAMETER;

  *DarwinVersion = 0;
  if ((Context == NULL) || (Context->Buffer == NULL) || (Context->Entries == NULL) ||
      (Context->KernelIndex >= Context->EntryCount) ||
      (Context->BufferSize > Context->BufferCapacity))
    return EFI_INVALID_PARAMETER;

  Offset = Context->Entries[Context->KernelIndex].FileOffset;
  Status = InternalReadMachHeader(Context->Buffer, Context->BufferSize, Offset, &Header);
  if (EFI_ERROR(Status))
    return Status;

  if (Header.FileType != AC_MACH_EXECUTE)
    return EFI_UNSUPPORTED;

  Offset += sizeof(Header);
  End = Offset + Header.CommandsSize;
  FoundVersion = 0;
  for (; Offset < End; Offset += Command.Size)
  {
    CopyMem(&Command, Context->Buffer + Offset, sizeof(Command));
    if (Command.Type != AC_MACH_LC_SEGMENT_64)
      continue;

    CopyMem(&Segment, Context->Buffer + Offset, sizeof(Segment));
    FileSize = (UINTN)Segment.FileSize;
    if (FileSize < sizeof(Prefix) - 1)
      continue;

    Bytes = Context->Buffer + (UINTN)Segment.FileOffset;
    for (Index = 0; Index <= FileSize - (sizeof(Prefix) - 1); ++Index)
    {
      if (CompareMem(Bytes + Index, Prefix, sizeof(Prefix) - 1) != 0)
        continue;

      Status = ParseVersion(Bytes + Index + sizeof(Prefix) - 1,
                            FileSize - Index - (sizeof(Prefix) - 1), &Version);
      if (EFI_ERROR(Status))
        return Status;

      if ((FoundVersion != 0) && (FoundVersion != Version))
        return EFI_COMPROMISED_DATA;

      FoundVersion = Version;
    }
  }

  if (FoundVersion == 0)
    return EFI_NOT_FOUND;

  *DarwinVersion = FoundVersion;
  return EFI_SUCCESS;
}
