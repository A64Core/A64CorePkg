/** @file
  ARM boot and PSCI declarations from FADT.
**/

#include "AcpiInternal.h"

EFI_STATUS
AcAcpiReadFadt(IN CONST AC_ACPI_CONTEXT *Context, OUT AC_ACPI_FADT_INFO *Info)
{
  CONST EFI_ACPI_DESCRIPTION_HEADER        *Table;
  EFI_ACPI_6_6_FIXED_ACPI_DESCRIPTION_TABLE Fadt;
  UINTN                                     PrefixSize =
    OFFSET_OF(EFI_ACPI_6_6_FIXED_ACPI_DESCRIPTION_TABLE, MinorVersion) + sizeof(Fadt.MinorVersion);
  AC_ACPI_FADT_INFO Result;
  EFI_STATUS        Status;

  if (Info == NULL)
    return EFI_INVALID_PARAMETER;
  ZeroMem(Info, sizeof(*Info));
  Status = InternalAcpiFindUniqueTable(Context, EFI_ACPI_6_6_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE,
                                       &Table);
  if (EFI_ERROR(Status))
    return Status;
  if (Table->Revision < 5)
    return EFI_UNSUPPORTED;
  if (Table->Length < PrefixSize)
    return EFI_COMPROMISED_DATA;
  CopyMem(&Fadt, Table, PrefixSize);
  if (Table->Revision == 5 && Fadt.MinorVersion < 1)
    return EFI_UNSUPPORTED;
  ZeroMem(&Result, sizeof(Result));
  Result.ArmBootArch = Fadt.ArmBootArch;
  Result.HardwareReducedAcpi = (Fadt.Flags & EFI_ACPI_6_6_HW_REDUCED_ACPI) != 0;
  if ((Fadt.ArmBootArch & EFI_ACPI_6_6_ARM_PSCI_COMPLIANT) != 0)
    Result.PsciConduit =
      (Fadt.ArmBootArch & EFI_ACPI_6_6_ARM_PSCI_USE_HVC) != 0 ? AcAcpiPsciHvc : AcAcpiPsciSmc;
  *Info = Result;
  return EFI_SUCCESS;
}
