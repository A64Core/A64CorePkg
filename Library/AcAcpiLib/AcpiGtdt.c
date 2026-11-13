/** @file
  Architectural timer descriptions from GTDT.
**/

#include "AcpiInternal.h"

STATIC EFI_STATUS
CheckPlatformTimers(IN CONST EFI_ACPI_DESCRIPTION_HEADER *Table, IN UINT32 Count, IN UINT32 Offset,
                    IN UINTN FixedSize)
{
  UINTN  Position = Offset;
  UINT32 Index;
  UINT16 Length;

  if (Count == 0)
    return EFI_SUCCESS;
  if (Position < FixedSize || Position > Table->Length || Count > (Table->Length - Position) / 3)
    return EFI_COMPROMISED_DATA;
  for (Index = 0; Index < Count; ++Index)
  {
    if (Table->Length - Position < 3)
      return EFI_COMPROMISED_DATA;
    CopyMem(&Length, (CONST UINT8 *)Table + Position + 1, sizeof(Length));
    if (Length < 3 || Length > Table->Length - Position)
      return EFI_COMPROMISED_DATA;
    Position += Length;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
AcAcpiReadGtdt(IN CONST AC_ACPI_CONTEXT *Context, OUT AC_ACPI_GTDT_INFO *Info)
{
  CONST EFI_ACPI_DESCRIPTION_HEADER           *Table;
  EFI_ACPI_6_6_GENERIC_TIMER_DESCRIPTION_TABLE Gtdt;
  AC_ACPI_GTDT_INFO                            Result;
  UINTN                                        FixedSize;
  EFI_STATUS                                   Status;

  if (Info == NULL)
    return EFI_INVALID_PARAMETER;
  ZeroMem(Info, sizeof(*Info));
  Status = InternalAcpiFindUniqueTable(
    Context, EFI_ACPI_6_6_GENERIC_TIMER_DESCRIPTION_TABLE_SIGNATURE, &Table);
  if (EFI_ERROR(Status))
    return Status;
  if (Table->Revision < 2)
    return EFI_UNSUPPORTED;
  FixedSize = Table->Revision == 2
                ? OFFSET_OF(EFI_ACPI_6_6_GENERIC_TIMER_DESCRIPTION_TABLE, VirtualPL2TimerGSIV)
                : sizeof(Gtdt);
  if (Table->Length < FixedSize)
    return EFI_COMPROMISED_DATA;
  ZeroMem(&Gtdt, sizeof(Gtdt));
  CopyMem(&Gtdt, Table, FixedSize);
  Status = CheckPlatformTimers(Table, Gtdt.PlatformTimerCount, Gtdt.PlatformTimerOffset, FixedSize);
  if (EFI_ERROR(Status))
    return Status;
  ZeroMem(&Result, sizeof(Result));
  Result.CounterControlBase = Gtdt.CntControlBasePhysicalAddress;
  Result.CounterReadBase = Gtdt.CntReadBasePhysicalAddress;
  Result.SecureEl1.Gsiv = Gtdt.SecurePL1TimerGSIV;
  Result.SecureEl1.Flags = Gtdt.SecurePL1TimerFlags;
  Result.NonSecureEl1.Gsiv = Gtdt.NonSecurePL1TimerGSIV;
  Result.NonSecureEl1.Flags = Gtdt.NonSecurePL1TimerFlags;
  Result.VirtualEl1.Gsiv = Gtdt.VirtualTimerGSIV;
  Result.VirtualEl1.Flags = Gtdt.VirtualTimerFlags;
  Result.El2.Gsiv = Gtdt.NonSecurePL2TimerGSIV;
  Result.El2.Flags = Gtdt.NonSecurePL2TimerFlags;
  Result.HasVirtualEl2 = Table->Revision >= 3;
  Result.VirtualEl2.Gsiv = Gtdt.VirtualPL2TimerGSIV;
  Result.VirtualEl2.Flags = Gtdt.VirtualPL2TimerFlags;
  *Info = Result;
  return EFI_SUCCESS;
}
