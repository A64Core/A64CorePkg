/** @file
  Unpacked AArch64 Mach-O payload inspection.
**/

#include "AppleKernelInternal.h"

EFI_STATUS
InternalReadMachHeader(IN CONST UINT8 *Buffer, IN UINTN BufferSize, IN UINTN HeaderOffset,
                       OUT AC_MACH_HEADER *Header)
{
  UINT32          Magic;
  UINT32          Index;
  UINTN           Offset;
  UINTN           End;
  AC_MACH_COMMAND Command;
  AC_MACH_SEGMENT Segment;
  AC_MACH_SYMTAB  Symtab;
  BOOLEAN         HasSymtab;

  if ((HeaderOffset > BufferSize) || (BufferSize - HeaderOffset < sizeof(Magic)))
    return EFI_COMPROMISED_DATA;

  CopyMem(&Magic, Buffer + HeaderOffset, sizeof(Magic));
  if (Magic != AC_MACH_MAGIC_64)
    return EFI_UNSUPPORTED;

  if (BufferSize - HeaderOffset < sizeof(*Header))
    return EFI_COMPROMISED_DATA;

  CopyMem(Header, Buffer + HeaderOffset, sizeof(*Header));
  if ((Header->CpuType != AC_MACH_CPU_ARM64) ||
      ((Header->FileType != AC_MACH_EXECUTE) && (Header->FileType != AC_MACH_KEXT_BUNDLE) &&
       (Header->FileType != AC_MACH_FILESET)))
    return EFI_UNSUPPORTED;

  Offset = HeaderOffset + sizeof(*Header);
  if ((Header->CommandsSize > BufferSize - Offset) ||
      (Header->CommandCount > Header->CommandsSize / sizeof(Command)))
    return EFI_COMPROMISED_DATA;

  End = Offset + Header->CommandsSize;
  HasSymtab = FALSE;
  for (Index = 0; Index < Header->CommandCount; ++Index)
  {
    if (End - Offset < sizeof(Command))
      return EFI_COMPROMISED_DATA;

    CopyMem(&Command, Buffer + Offset, sizeof(Command));
    if ((Command.Size < sizeof(Command)) || ((Command.Size & 7U) != 0) ||
        (Command.Size > End - Offset))
      return EFI_COMPROMISED_DATA;

    if (Command.Type == AC_MACH_LC_SEGMENT_64)
    {
      if (Command.Size < sizeof(Segment))
        return EFI_COMPROMISED_DATA;

      CopyMem(&Segment, Buffer + Offset, sizeof(Segment));
      if ((Segment.SectionCount > (Command.Size - sizeof(Segment)) / AC_MACH_SECTION_64_SIZE) ||
          (Segment.FileOffset > BufferSize) ||
          (Segment.FileSize > BufferSize - Segment.FileOffset) ||
          (Segment.VirtualSize > MAX_UINT64 - Segment.VirtualAddress))
        return EFI_COMPROMISED_DATA;
    }
    else if (Command.Type == AC_MACH_LC_SYMTAB)
    {
      if (HasSymtab || (Command.Size != sizeof(Symtab)))
        return EFI_COMPROMISED_DATA;

      CopyMem(&Symtab, Buffer + Offset, sizeof(Symtab));
      if ((Symtab.SymbolsOffset > BufferSize) ||
          (Symtab.SymbolCount > (BufferSize - Symtab.SymbolsOffset) / sizeof(AC_MACH_SYMBOL)) ||
          (Symtab.StringsOffset > BufferSize) ||
          (Symtab.StringsSize > BufferSize - Symtab.StringsOffset))
        return EFI_COMPROMISED_DATA;

      HasSymtab = TRUE;
    }

    Offset += Command.Size;
  }

  return Offset == End ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
}

EFI_STATUS
AcKernelIdentifyPayload(IN CONST VOID *Buffer, IN UINTN BufferSize,
                        OUT AC_KERNEL_PAYLOAD_TYPE *Type)
{
  EFI_STATUS     Status;
  AC_MACH_HEADER Header;

  if (Type == NULL)
    return EFI_INVALID_PARAMETER;

  *Type = AcKernelPayloadUnknown;
  if (Buffer == NULL)
    return EFI_INVALID_PARAMETER;

  Status = InternalReadMachHeader(Buffer, BufferSize, 0, &Header);
  if (EFI_ERROR(Status))
    return Status;

  if (Header.FileType == AC_MACH_FILESET)
    *Type = AcKernelPayloadCollection;
  else if (Header.FileType == AC_MACH_EXECUTE)
    *Type = AcKernelPayloadMachO;
  else
    return EFI_UNSUPPORTED;

  return EFI_SUCCESS;
}
