/** @file
  ARM MADT CPU and interrupt-controller descriptions.
**/

#include "AcpiInternal.h"

#define AC_ACPI_MPIDR_AFFINITY_MASK 0xFF00FFFFFFULL
#define AC_ACPI_GICC_MIN_SIZE OFFSET_OF(EFI_ACPI_6_6_GIC_STRUCTURE, ProcessorPowerEfficiencyClass)

STATIC EFI_STATUS
ReadEntry(IN CONST EFI_ACPI_DESCRIPTION_HEADER *Table, IN UINTN Offset, OUT UINT8 *Type,
          OUT UINT8 *Length)
{
  CONST UINT8 *Entry = (CONST UINT8 *)Table + Offset;
  UINTN        Minimum = 2;

  if (Table->Length - Offset < 2)
    return EFI_COMPROMISED_DATA;
  *Type = Entry[0];
  *Length = Entry[1];
  switch (*Type)
  {
  case EFI_ACPI_6_6_GIC:
    Minimum = AC_ACPI_GICC_MIN_SIZE;
    break;
  case EFI_ACPI_6_6_GICD:
    Minimum = sizeof(EFI_ACPI_6_6_GIC_DISTRIBUTOR_STRUCTURE);
    break;
  case EFI_ACPI_6_6_GICR:
    Minimum = sizeof(EFI_ACPI_6_6_GICR_STRUCTURE);
    break;
  default:
    break;
  }
  if (*Length < Minimum || *Length > Table->Length - Offset)
    return EFI_COMPROMISED_DATA;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ValidateDescriptions(IN CONST AC_ACPI_MADT_INFO *Info)
{
  CONST AC_ACPI_CPU_INFO   *Cpu;
  CONST AC_ACPI_CPU_INFO   *Previous;
  CONST AC_ACPI_GICR_RANGE *Range;
  CONST AC_ACPI_GICR_RANGE *PreviousRange;
  UINT32                    Available = EFI_ACPI_6_6_GIC_ENABLED | EFI_ACPI_6_6_GIC_ONLINE_CAPABLE;
  UINTN                     Index;
  UINTN                     Other;

  for (Index = 0; Index < Info->CpuCount; ++Index)
  {
    Cpu = &Info->Cpus[Index];
    for (Other = 0; Other < Index; ++Other)
    {
      Previous = &Info->Cpus[Other];
      if (Cpu->AcpiUid == Previous->AcpiUid ||
          ((Cpu->Flags & Available) != 0 && (Previous->Flags & Available) != 0 &&
           (Cpu->Mpidr & AC_ACPI_MPIDR_AFFINITY_MASK) ==
             (Previous->Mpidr & AC_ACPI_MPIDR_AFFINITY_MASK)))
        return EFI_COMPROMISED_DATA;
    }
  }
  if (Info->RedistributorRangeCount != 0 && Info->GicVersion != 0 && Info->GicVersion < 3)
    return EFI_COMPROMISED_DATA;
  for (Index = 0; Index < Info->RedistributorRangeCount; ++Index)
  {
    Range = &Info->RedistributorRanges[Index];
    if (Range->Length == 0 || Range->Base > MAX_UINT64 - Range->Length)
      return EFI_COMPROMISED_DATA;
    for (Other = 0; Other < Index; ++Other)
    {
      PreviousRange = &Info->RedistributorRanges[Other];
      if (Range->Base < PreviousRange->Base + PreviousRange->Length &&
          PreviousRange->Base < Range->Base + Range->Length)
        return EFI_COMPROMISED_DATA;
    }
  }
  return EFI_SUCCESS;
}

VOID
AcAcpiFreeMadt(IN OUT AC_ACPI_MADT_INFO *Info OPTIONAL)
{
  if (Info == NULL)
    return;
  if (Info->Cpus != NULL)
    FreePool(Info->Cpus);
  if (Info->RedistributorRanges != NULL)
    FreePool(Info->RedistributorRanges);
  ZeroMem(Info, sizeof(*Info));
}

EFI_STATUS
AcAcpiReadMadt(IN CONST AC_ACPI_CONTEXT *Context, OUT AC_ACPI_MADT_INFO *Info)
{
  CONST EFI_ACPI_DESCRIPTION_HEADER     *Table;
  EFI_ACPI_6_6_GIC_STRUCTURE             Gicc;
  EFI_ACPI_6_6_GIC_DISTRIBUTOR_STRUCTURE Gicd;
  EFI_ACPI_6_6_GICR_STRUCTURE            Gicr;
  AC_ACPI_MADT_INFO                      Result;
  AC_ACPI_CPU_INFO                      *Cpu;
  UINTN                                  Offset;
  UINTN                                  CpuIndex = 0;
  UINTN                                  RangeIndex = 0;
  UINT8                                  Type;
  UINT8                                  Length;
  BOOLEAN                                HasDistributor = FALSE;
  EFI_STATUS                             Status;

  if (Info == NULL)
    return EFI_INVALID_PARAMETER;
  ZeroMem(Info, sizeof(*Info));
  ZeroMem(&Result, sizeof(Result));
  Status = InternalAcpiFindUniqueTable(
    Context, EFI_ACPI_6_6_MULTIPLE_APIC_DESCRIPTION_TABLE_SIGNATURE, &Table);
  if (EFI_ERROR(Status))
    return Status;
  if (Table->Revision < 3)
    return EFI_UNSUPPORTED;
  if (Table->Length < sizeof(EFI_ACPI_6_6_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER))
    return EFI_COMPROMISED_DATA;

  for (Offset = sizeof(EFI_ACPI_6_6_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER); Offset < Table->Length;
       Offset += Length)
  {
    Status = ReadEntry(Table, Offset, &Type, &Length);
    if (EFI_ERROR(Status))
      return Status;
    if (Type == EFI_ACPI_6_6_GIC)
    {
      if (++Result.CpuCount > AC_ACPI_MAX_CPUS)
        return EFI_BAD_BUFFER_SIZE;
    }
    else if (Type == EFI_ACPI_6_6_GICR)
    {
      if (++Result.RedistributorRangeCount > AC_ACPI_MAX_GICR_RANGES)
        return EFI_BAD_BUFFER_SIZE;
    }
    else if (Type == EFI_ACPI_6_6_GICD)
    {
      if (HasDistributor)
        return EFI_COMPROMISED_DATA;
      HasDistributor = TRUE;
      CopyMem(&Gicd, (CONST UINT8 *)Table + Offset, sizeof(Gicd));
      Result.DistributorBase = Gicd.PhysicalBaseAddress;
      Result.GicVersion = Gicd.GicVersion;
    }
  }
  if (!HasDistributor && Result.CpuCount == 0 && Result.RedistributorRangeCount == 0)
    return EFI_UNSUPPORTED;
  if (!HasDistributor || Result.CpuCount == 0)
    return EFI_COMPROMISED_DATA;
  Result.Cpus = AllocateZeroPool(Result.CpuCount * sizeof(*Result.Cpus));
  if (Result.Cpus == NULL)
    return EFI_OUT_OF_RESOURCES;
  if (Result.RedistributorRangeCount != 0)
  {
    Result.RedistributorRanges =
      AllocateZeroPool(Result.RedistributorRangeCount * sizeof(*Result.RedistributorRanges));
    if (Result.RedistributorRanges == NULL)
    {
      Status = EFI_OUT_OF_RESOURCES;
      goto Failure;
    }
  }
  for (Offset = sizeof(EFI_ACPI_6_6_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER); Offset < Table->Length;
       Offset += Length)
  {
    Status = ReadEntry(Table, Offset, &Type, &Length);
    if (EFI_ERROR(Status))
      goto Failure;
    if (Type == EFI_ACPI_6_6_GIC)
    {
      CopyMem(&Gicc, (CONST UINT8 *)Table + Offset, AC_ACPI_GICC_MIN_SIZE);
      Cpu = &Result.Cpus[CpuIndex++];
      Cpu->AcpiUid = Gicc.AcpiProcessorUid;
      Cpu->Mpidr = Gicc.MPIDR;
      Cpu->Flags = Gicc.Flags;
      Cpu->CpuInterfaceNumber = Gicc.CPUInterfaceNumber;
      Cpu->CpuInterfaceBase = Gicc.PhysicalBaseAddress;
      Cpu->RedistributorBase = Gicc.GICRBaseAddress;
    }
    else if (Type == EFI_ACPI_6_6_GICR)
    {
      CopyMem(&Gicr, (CONST UINT8 *)Table + Offset, sizeof(Gicr));
      Result.RedistributorRanges[RangeIndex].Base = Gicr.DiscoveryRangeBaseAddress;
      Result.RedistributorRanges[RangeIndex].Length = Gicr.DiscoveryRangeLength;
      Result.RedistributorRanges[RangeIndex].Flags = *((CONST UINT8 *)Table + Offset + 2);
      ++RangeIndex;
    }
  }
  Status = ValidateDescriptions(&Result);
  if (EFI_ERROR(Status))
    goto Failure;
  *Info = Result;
  return EFI_SUCCESS;

Failure:
  AcAcpiFreeMadt(&Result);
  return Status;
}
