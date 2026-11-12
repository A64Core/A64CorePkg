/** @file
  Firmware ACPI discovery and context ownership.
**/

#include "AcpiInternal.h"

STATIC EFI_STATUS
ReadMemoryMap(IN OUT AC_ACPI_CONTEXT *Context)
{
  EFI_MEMORY_DESCRIPTOR *Map;
  UINTN                  Size = 0;
  UINTN                  Capacity;
  UINTN                  Key;
  UINTN                  DescriptorSize = 0;
  UINT32                 Version;
  UINTN                  Attempt;
  EFI_STATUS             Status;

  Status = gBS->GetMemoryMap(&Size, NULL, &Key, &DescriptorSize, &Version);
  if (Status != EFI_BUFFER_TOO_SMALL)
    return EFI_ERROR(Status) ? Status : EFI_COMPROMISED_DATA;
  for (Attempt = 0; Attempt < 8; ++Attempt)
  {
    if (DescriptorSize < sizeof(*Map) || Size == 0)
      return EFI_COMPROMISED_DATA;
    if (DescriptorSize > AC_ACPI_MAX_SIZE / 2 || Size > AC_ACPI_MAX_SIZE - 2 * DescriptorSize)
      return EFI_BAD_BUFFER_SIZE;
    Capacity = Size + 2 * DescriptorSize;
    Map = AllocatePool(Capacity);
    if (Map == NULL)
      return EFI_OUT_OF_RESOURCES;
    Size = Capacity;
    Status = gBS->GetMemoryMap(&Size, Map, &Key, &DescriptorSize, &Version);
    if (!EFI_ERROR(Status))
    {
      if (Size == 0 || Size > Capacity || DescriptorSize < sizeof(*Map) ||
          Size % DescriptorSize != 0)
      {
        FreePool(Map);
        return EFI_COMPROMISED_DATA;
      }
      Context->MemoryMap = Map;
      Context->MemoryMapSize = Size;
      Context->DescriptorSize = DescriptorSize;
      return EFI_SUCCESS;
    }
    FreePool(Map);
    if (Status != EFI_BUFFER_TOO_SMALL)
      return Status;
  }
  return EFI_ABORTED;
}

EFI_STATUS
AcAcpiOpen(OUT AC_ACPI_CONTEXT **Context)
{
  AC_ACPI_CONTEXT                             *Result;
  EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER Rsdp;
  CONST VOID                                  *Source = NULL;
  UINTN                                        Index;
  UINTN                                        Count;
  EFI_STATUS                                   Status;
  BOOLEAN                                      Legacy = FALSE;

  if (Context == NULL)
    return EFI_INVALID_PARAMETER;
  *Context = NULL;
  if (gST == NULL || gBS == NULL || gBS->GetMemoryMap == NULL)
    return EFI_UNSUPPORTED;
  if (gST->NumberOfTableEntries != 0 && gST->ConfigurationTable == NULL)
    return EFI_COMPROMISED_DATA;
  for (Index = 0; Index < gST->NumberOfTableEntries; ++Index)
  {
    if (CompareGuid(&gST->ConfigurationTable[Index].VendorGuid, &gEfiAcpi20TableGuid))
    {
      Source = gST->ConfigurationTable[Index].VendorTable;
      if (Source == NULL)
        return EFI_COMPROMISED_DATA;
      break;
    }
    if (CompareGuid(&gST->ConfigurationTable[Index].VendorGuid, &gEfiAcpi10TableGuid))
      Legacy = TRUE;
  }
  if (Source == NULL)
    return Legacy ? EFI_UNSUPPORTED : EFI_NOT_FOUND;
  Result = AllocateZeroPool(sizeof(*Result));
  if (Result == NULL)
    return EFI_OUT_OF_RESOURCES;
  Status = ReadMemoryMap(Result);
  if (EFI_ERROR(Status))
    goto Failure;
  if (!InternalAcpiReadable(Result, (UINTN)Source, 20))
  {
    Status = EFI_COMPROMISED_DATA;
    goto Failure;
  }
  ZeroMem(&Rsdp, sizeof(Rsdp));
  CopyMem(&Rsdp, Source, 20);
  if (Rsdp.Signature != EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER_SIGNATURE ||
      CalculateSum8(Source, 20) != 0)
  {
    Status = EFI_COMPROMISED_DATA;
    goto Failure;
  }
  if (Rsdp.Revision < EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER_REVISION)
  {
    Status = EFI_UNSUPPORTED;
    goto Failure;
  }
  if (!InternalAcpiReadable(Result, (UINTN)Source, sizeof(Rsdp)))
  {
    Status = EFI_COMPROMISED_DATA;
    goto Failure;
  }
  CopyMem(&Rsdp, Source, sizeof(Rsdp));
  if (Rsdp.Length > AC_ACPI_MAX_SIZE)
  {
    Status = EFI_BAD_BUFFER_SIZE;
    goto Failure;
  }
  if (Rsdp.Length < sizeof(Rsdp) || !InternalAcpiReadable(Result, (UINTN)Source, Rsdp.Length) ||
      CalculateSum8(Source, Rsdp.Length) != 0)
  {
    Status = EFI_COMPROMISED_DATA;
    goto Failure;
  }
  if (Rsdp.XsdtAddress == 0)
  {
    Status = EFI_UNSUPPORTED;
    goto Failure;
  }
  Status = InternalAcpiReadTable(Result, Rsdp.XsdtAddress, &Result->Xsdt);
  if (EFI_ERROR(Status))
    goto Failure;
  Count = Result->Xsdt->Length - sizeof(*Result->Xsdt);
  if (Result->Xsdt->Signature != EFI_ACPI_2_0_EXTENDED_SYSTEM_DESCRIPTION_TABLE_SIGNATURE ||
      Count % sizeof(UINT64) != 0)
  {
    Status = EFI_COMPROMISED_DATA;
    goto Failure;
  }
  Result->TableCount = Count / sizeof(UINT64);
  if (Result->TableCount > AC_ACPI_MAX_TABLES)
  {
    Status = EFI_BAD_BUFFER_SIZE;
    goto Failure;
  }
  *Context = Result;
  return EFI_SUCCESS;

Failure:
  AcAcpiClose(Result);
  return Status;
}

VOID
AcAcpiClose(IN AC_ACPI_CONTEXT *Context OPTIONAL)
{
  if (Context == NULL)
    return;
  if (Context->MemoryMap != NULL)
    FreePool(Context->MemoryMap);
  FreePool(Context);
}
