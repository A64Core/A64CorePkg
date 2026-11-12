/** @file
  Read-only firmware ACPI table access.
**/

#ifndef AC_ACPI_LIB_H
#define AC_ACPI_LIB_H

#include <Uefi.h>
#include <IndustryStandard/Acpi.h>

typedef struct AC_ACPI_CONTEXT AC_ACPI_CONTEXT;

/**
  Locate and validate the firmware's ACPI 2.0-or-newer RSDP and XSDT.

  Context owns a UEFI memory-map snapshot and borrows the firmware tables, which
  must remain unchanged until Close. Open, table access, and Close require Boot
  Services. No firmware memory is modified. Context is NULL on failure.

  Only XSDT direct children are enumerated. Legacy RSDT traversal, FADT-linked
  DSDT/FACS discovery, and AML are unsupported.
  Limits are 16 MiB per table or memory map, and 4096 XSDT entries.

  Missing ACPI returns EFI_NOT_FOUND; a legacy-only entry or absent XSDT returns
  EFI_UNSUPPORTED. Invalid ranges, checksums, or framing return
  EFI_COMPROMISED_DATA. Limits return EFI_BAD_BUFFER_SIZE, allocation failure
  EFI_OUT_OF_RESOURCES, and invalid arguments EFI_INVALID_PARAMETER.
  GetMemoryMap errors are propagated; eight consecutive map-growth retries
  return EFI_ABORTED.
**/
EFI_STATUS AcAcpiOpen(OUT AC_ACPI_CONTEXT **Context);

/** Free only the context and its memory-map snapshot. NULL is allowed. **/
VOID AcAcpiClose(IN AC_ACPI_CONTEXT *Context OPTIONAL);

/** Return the XSDT direct-entry count, including duplicate entries; NULL returns zero. **/
UINTN AcAcpiGetTableCount(IN CONST AC_ACPI_CONTEXT *Context OPTIONAL);

/**
  Validate a direct child by zero-based XSDT index and return a borrowed view.

  Table is NULL on failure; a past-end index returns EFI_NOT_FOUND. The header
  and its Length bytes are validated before returning. Unknown signatures are
  allowed. The view expires at Close or ExitBootServices, whichever comes first.
**/
EFI_STATUS
AcAcpiGetTable(IN CONST AC_ACPI_CONTEXT *Context, IN UINTN Index,
               OUT CONST EFI_ACPI_DESCRIPTION_HEADER **Table);

/**
  Find a signature's zero-based instance in XSDT order.

  Every visited table is validated; errors are returned without skipping a
  corrupt table. Missing instances return EFI_NOT_FOUND. Ownership and failure
  output follow AcAcpiGetTable().
**/
EFI_STATUS
AcAcpiFindTable(IN CONST AC_ACPI_CONTEXT *Context, IN UINT32 Signature, IN UINTN Instance,
                OUT CONST EFI_ACPI_DESCRIPTION_HEADER **Table);

#endif
