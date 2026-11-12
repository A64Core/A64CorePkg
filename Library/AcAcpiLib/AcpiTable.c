/** @file
  ACPI physical range checks, table validation, and lookup.
**/

#include "AcpiInternal.h"

BOOLEAN
InternalAcpiReadable(IN CONST AC_ACPI_CONTEXT *Context, IN UINT64 Address, IN UINTN Size)
{
  EFI_MEMORY_DESCRIPTOR Descriptor;
  UINT64                Position = Address;
  UINT64                End;
  UINT64                Covered;
  UINT64                DescriptorEnd;
  UINTN                 Offset;

  if (Address == 0 || Size == 0 || Address > MAX_UINTN || Size > MAX_UINTN - (UINTN)Address)
    return FALSE;
  End = Address + Size;
  while (Position < End)
  {
    Covered = Position;
    for (Offset = 0; Offset < Context->MemoryMapSize; Offset += Context->DescriptorSize)
    {
      CopyMem(&Descriptor, (CONST UINT8 *)Context->MemoryMap + Offset, sizeof(Descriptor));
      switch (Descriptor.Type)
      {
      case EfiLoaderCode:
      case EfiLoaderData:
      case EfiBootServicesCode:
      case EfiBootServicesData:
      case EfiRuntimeServicesCode:
      case EfiRuntimeServicesData:
      case EfiConventionalMemory:
      case EfiACPIReclaimMemory:
      case EfiACPIMemoryNVS:
        break;
      default:
        continue;
      }
      if ((Descriptor.Attribute & EFI_MEMORY_RP) != 0 || Descriptor.PhysicalStart > Position ||
          Descriptor.NumberOfPages > (MAX_UINT64 - Descriptor.PhysicalStart) / EFI_PAGE_SIZE)
        continue;
      DescriptorEnd = Descriptor.PhysicalStart + Descriptor.NumberOfPages * EFI_PAGE_SIZE;
      if (DescriptorEnd > Covered)
        Covered = DescriptorEnd;
    }
    if (Covered == Position)
      return FALSE;
    Position = MIN(Covered, End);
  }
  return TRUE;
}

EFI_STATUS
InternalAcpiReadTable(IN CONST AC_ACPI_CONTEXT *Context, IN UINT64 Address,
                      OUT CONST EFI_ACPI_DESCRIPTION_HEADER **Table)
{
  EFI_ACPI_DESCRIPTION_HEADER Header;
  CONST VOID                 *Source = (CONST VOID *)(UINTN)Address;

  *Table = NULL;
  if (!InternalAcpiReadable(Context, Address, sizeof(Header)))
    return EFI_COMPROMISED_DATA;
  CopyMem(&Header, Source, sizeof(Header));
  if (Header.Length < sizeof(Header))
    return EFI_COMPROMISED_DATA;
  if (Header.Length > AC_ACPI_MAX_SIZE)
    return EFI_BAD_BUFFER_SIZE;
  if (!InternalAcpiReadable(Context, Address, Header.Length) ||
      CalculateSum8(Source, Header.Length) != 0)
    return EFI_COMPROMISED_DATA;
  *Table = Source;
  return EFI_SUCCESS;
}

UINTN
AcAcpiGetTableCount(IN CONST AC_ACPI_CONTEXT *Context OPTIONAL)
{
  return Context == NULL ? 0 : Context->TableCount;
}

EFI_STATUS
AcAcpiGetTable(IN CONST AC_ACPI_CONTEXT *Context, IN UINTN Index,
               OUT CONST EFI_ACPI_DESCRIPTION_HEADER **Table)
{
  UINT64 Address;

  if (Table == NULL)
    return EFI_INVALID_PARAMETER;
  *Table = NULL;
  if (Context == NULL)
    return EFI_INVALID_PARAMETER;
  if (Index >= Context->TableCount)
    return EFI_NOT_FOUND;
  CopyMem(&Address, (CONST UINT8 *)Context->Xsdt + sizeof(*Context->Xsdt) + Index * sizeof(Address),
          sizeof(Address));
  return InternalAcpiReadTable(Context, Address, Table);
}

EFI_STATUS
AcAcpiFindTable(IN CONST AC_ACPI_CONTEXT *Context, IN UINT32 Signature, IN UINTN Instance,
                OUT CONST EFI_ACPI_DESCRIPTION_HEADER **Table)
{
  CONST EFI_ACPI_DESCRIPTION_HEADER *Candidate;
  UINTN                              Index;
  EFI_STATUS                         Status;

  if (Table == NULL)
    return EFI_INVALID_PARAMETER;
  *Table = NULL;
  if (Context == NULL)
    return EFI_INVALID_PARAMETER;
  for (Index = 0; Index < Context->TableCount; ++Index)
  {
    Status = AcAcpiGetTable(Context, Index, &Candidate);
    if (EFI_ERROR(Status))
      return Status;
    if (Candidate->Signature == Signature)
    {
      if (Instance == 0)
      {
        *Table = Candidate;
        return EFI_SUCCESS;
      }
      --Instance;
    }
  }
  return EFI_NOT_FOUND;
}

EFI_STATUS
InternalAcpiFindUniqueTable(IN CONST AC_ACPI_CONTEXT *Context, IN UINT32 Signature,
                            OUT CONST EFI_ACPI_DESCRIPTION_HEADER **Table)
{
  CONST EFI_ACPI_DESCRIPTION_HEADER *Candidate;
  CONST EFI_ACPI_DESCRIPTION_HEADER *Found = NULL;
  UINTN                              Index;
  EFI_STATUS                         Status;

  *Table = NULL;
  if (Context == NULL)
    return EFI_INVALID_PARAMETER;
  for (Index = 0; Index < Context->TableCount; ++Index)
  {
    Status = AcAcpiGetTable(Context, Index, &Candidate);
    if (EFI_ERROR(Status))
      return Status;
    if (Candidate->Signature != Signature)
      continue;
    if (Found != NULL)
      return EFI_COMPROMISED_DATA;
    Found = Candidate;
  }
  if (Found == NULL)
    return EFI_NOT_FOUND;
  *Table = Found;
  return EFI_SUCCESS;
}
