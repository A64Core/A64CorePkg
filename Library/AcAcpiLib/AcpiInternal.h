/** @file
  Firmware ACPI access implementation details.
**/

#ifndef AC_ACPI_INTERNAL_H
#define AC_ACPI_INTERNAL_H

#include <Library/AcAcpiLib.h>
#include <Guid/Acpi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define AC_ACPI_MAX_SIZE (16U * 1024U * 1024U)
#define AC_ACPI_MAX_TABLES 4096U
#define AC_ACPI_MAX_CPUS 4096U
#define AC_ACPI_MAX_GICR_RANGES 4096U

struct AC_ACPI_CONTEXT
{
  EFI_MEMORY_DESCRIPTOR             *MemoryMap;
  UINTN                              MemoryMapSize;
  UINTN                              DescriptorSize;
  CONST EFI_ACPI_DESCRIPTION_HEADER *Xsdt;
  UINTN                              TableCount;
};

BOOLEAN InternalAcpiReadable(IN CONST AC_ACPI_CONTEXT *Context, IN UINT64 Address, IN UINTN Size);

EFI_STATUS
InternalAcpiReadTable(IN CONST AC_ACPI_CONTEXT *Context, IN UINT64 Address,
                      OUT CONST EFI_ACPI_DESCRIPTION_HEADER **Table);

EFI_STATUS
InternalAcpiFindUniqueTable(IN CONST AC_ACPI_CONTEXT *Context, IN UINT32 Signature,
                            OUT CONST EFI_ACPI_DESCRIPTION_HEADER **Table);

#endif
