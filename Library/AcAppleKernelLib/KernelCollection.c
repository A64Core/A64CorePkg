/** @file
  Kernel collection indexing and context lifetime management.
**/

#include "AppleKernelInternal.h"

EFI_STATUS
AcKernelCollectionInit(OUT AC_KERNEL_COLLECTION_CONTEXT *Context, IN OUT VOID *Buffer,
                       IN UINTN BufferSize, IN UINTN BufferCapacity)
{
  EFI_STATUS                  Status;
  AC_MACH_HEADER              Header;
  AC_MACH_HEADER              ImageHeader;
  AC_MACH_COMMAND             Command;
  AC_MACH_FILESET_ENTRY       Entry;
  AC_KERNEL_COLLECTION_ENTRY *Entries;
  UINT8                      *Bytes;
  CONST CHAR8                *Identifier;
  UINTN                       Offset;
  UINTN                       End;
  UINTN                       StringIndex;
  UINT32                      EntryCount;
  UINT32                      Index;
  UINT32                      Previous;
  UINT32                      KernelIndex;

  if (Context == NULL)
    return EFI_INVALID_PARAMETER;

  ZeroMem(Context, sizeof(*Context));
  if ((Buffer == NULL) || (BufferSize > BufferCapacity))
    return EFI_INVALID_PARAMETER;

  Bytes = Buffer;
  Status = InternalReadMachHeader(Bytes, BufferSize, 0, &Header);
  if (EFI_ERROR(Status))
    return Status;

  if (Header.FileType != AC_MACH_FILESET)
    return EFI_UNSUPPORTED;

  End = sizeof(Header) + Header.CommandsSize;
  EntryCount = 0;
  for (Offset = sizeof(Header); Offset < End; Offset += Command.Size)
  {
    CopyMem(&Command, Bytes + Offset, sizeof(Command));
    if (Command.Type == AC_MACH_LC_FILESET_ENTRY)
      ++EntryCount;
  }

  if (EntryCount == 0)
    return EFI_NOT_FOUND;

  Entries = AllocateZeroPool(EntryCount * sizeof(*Entries));
  if (Entries == NULL)
    return EFI_OUT_OF_RESOURCES;

  Index = 0;
  KernelIndex = MAX_UINT32;
  for (Offset = sizeof(Header); Offset < End; Offset += Command.Size)
  {
    CopyMem(&Command, Bytes + Offset, sizeof(Command));
    if (Command.Type != AC_MACH_LC_FILESET_ENTRY)
      continue;

    Status = EFI_COMPROMISED_DATA;
    if (Command.Size <= sizeof(Entry))
      goto Error;

    CopyMem(&Entry, Bytes + Offset, sizeof(Entry));
    if ((Entry.IdentifierOffset < sizeof(Entry)) || (Entry.IdentifierOffset >= Command.Size) ||
        (Entry.FileOffset < End) || ((Entry.FileOffset & 7U) != 0) ||
        (Entry.FileOffset > BufferSize) || (BufferSize - Entry.FileOffset < sizeof(ImageHeader)))
      goto Error;

    Identifier = (CONST CHAR8 *)(Bytes + Offset + Entry.IdentifierOffset);
    for (StringIndex = Entry.IdentifierOffset; StringIndex < Command.Size; ++StringIndex)
    {
      if (Bytes[Offset + StringIndex] == '\0')
        break;
    }

    if ((StringIndex == Command.Size) || (Identifier[0] == '\0'))
      goto Error;

    for (Previous = 0; Previous < Index; ++Previous)
    {
      if ((Entries[Previous].FileOffset == Entry.FileOffset) ||
          (AsciiStrCmp(Entries[Previous].Identifier, Identifier) == 0))
        goto Error;
    }

    Status = InternalReadMachHeader(Bytes, BufferSize, (UINTN)Entry.FileOffset, &ImageHeader);
    if (EFI_ERROR(Status))
      goto Error;

    if (ImageHeader.FileType == AC_MACH_FILESET)
    {
      Status = EFI_UNSUPPORTED;
      goto Error;
    }

    if (AsciiStrCmp(Identifier, "com.apple.kernel") == 0)
    {
      if (ImageHeader.FileType != AC_MACH_EXECUTE)
      {
        Status = EFI_COMPROMISED_DATA;
        goto Error;
      }

      KernelIndex = Index;
    }

    Entries[Index].Identifier = Identifier;
    Entries[Index].FileOffset = (UINTN)Entry.FileOffset;
    Entries[Index].VirtualAddress = Entry.VirtualAddress;
    ++Index;
  }

  if (KernelIndex == MAX_UINT32)
  {
    Status = EFI_NOT_FOUND;
    goto Error;
  }

  Context->Buffer = Buffer;
  Context->BufferSize = BufferSize;
  Context->BufferCapacity = BufferCapacity;
  Context->Entries = Entries;
  Context->EntryCount = EntryCount;
  Context->KernelIndex = KernelIndex;
  return EFI_SUCCESS;

Error:
  FreePool(Entries);
  return Status;
}

VOID
AcKernelCollectionFree(IN OUT AC_KERNEL_COLLECTION_CONTEXT *Context OPTIONAL)
{
  if (Context == NULL)
    return;

  if (Context->Entries != NULL)
    FreePool(Context->Entries);

  ZeroMem(Context, sizeof(*Context));
}
