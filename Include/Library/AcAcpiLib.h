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

typedef struct
{
  UINT32 AcpiUid;
  UINT64 Mpidr;
  UINT32 Flags;
  UINT32 CpuInterfaceNumber;
  UINT64 CpuInterfaceBase;
  UINT64 RedistributorBase;
} AC_ACPI_CPU_INFO;

typedef struct
{
  UINT64 Base;
  UINT32 Length;
  UINT8  Flags;
} AC_ACPI_GICR_RANGE;

typedef struct
{
  UINTN               CpuCount;
  AC_ACPI_CPU_INFO   *Cpus;
  UINT64              DistributorBase;
  UINT8               GicVersion;
  UINTN               RedistributorRangeCount;
  AC_ACPI_GICR_RANGE *RedistributorRanges;
} AC_ACPI_MADT_INFO;

/**
  Read a unique MADT's GICC, GICD, and GICR descriptions without accessing MMIO.

  MADT revision 3 or newer and the known structure prefixes are supported.
  Unknown subtypes and extended tails are skipped after framing checks.
  Exactly one GICD and at least one GICC are required; a MADT without any of
  these ARM descriptions returns EFI_UNSUPPORTED. Limits are 4096 CPUs and
  4096 redistributor ranges. Duplicate UIDs, duplicate affinities among CPUs
  marked enabled or online-capable, and overlapping GICR ranges are rejected.

  CPU order and raw flags are preserved, including disabled CPUs. GIC version
  zero means unspecified. When RedistributorRangeCount is nonzero, the GICC
  RedistributorBase values must be ignored in favor of the discovery ranges.
  Register addresses are values, not mapped or validated hardware resources.

  Info must be empty or previously freed. It owns both arrays on success and
  retains no firmware pointers; release it with AcAcpiFreeMadt(). Failure clears
  Info. Reads require Boot Services and propagate table-access errors. Missing
  tables return EFI_NOT_FOUND, duplicate tables or malformed descriptions
  EFI_COMPROMISED_DATA, limits EFI_BAD_BUFFER_SIZE, allocation failure
  EFI_OUT_OF_RESOURCES, and invalid arguments EFI_INVALID_PARAMETER.
**/
EFI_STATUS AcAcpiReadMadt(IN CONST AC_ACPI_CONTEXT *Context, OUT AC_ACPI_MADT_INFO *Info);

/** Free arrays with Boot Services and clear Info. NULL and zero-initialized Info are allowed. **/
VOID AcAcpiFreeMadt(IN OUT AC_ACPI_MADT_INFO *Info OPTIONAL);

typedef struct
{
  UINT32 Gsiv;
  UINT32 Flags;
} AC_ACPI_TIMER_INTERRUPT;

typedef struct
{
  UINT64                  CounterControlBase;
  UINT64                  CounterReadBase;
  AC_ACPI_TIMER_INTERRUPT SecureEl1;
  AC_ACPI_TIMER_INTERRUPT NonSecureEl1;
  AC_ACPI_TIMER_INTERRUPT VirtualEl1;
  AC_ACPI_TIMER_INTERRUPT El2;
  AC_ACPI_TIMER_INTERRUPT VirtualEl2;
  BOOLEAN                 HasVirtualEl2;
} AC_ACPI_GTDT_INFO;

/**
  Copy a unique GTDT's architectural timer descriptions into caller-owned Info.

  Revision 2 is supported without Virtual EL2; revision 3 and newer include
  those fields. HasVirtualEl2 reports field availability, not implemented
  hardware: a zero GSIV indicates no Virtual EL2 timer was described. Counter
  bases of MAX_UINT64 mean not supplied. GSIVs and flags are not translated.
  Platform timer records receive outer framing checks only; GT block frames
  and watchdog operation are not interpreted. No timer, MMIO, or frequency
  register is accessed. Info has no allocations or borrowed pointers.

  Failure clears Info. Reads require Boot Services. Unsupported revisions
  return EFI_UNSUPPORTED; other errors follow AcAcpiReadMadt().
**/
EFI_STATUS AcAcpiReadGtdt(IN CONST AC_ACPI_CONTEXT *Context, OUT AC_ACPI_GTDT_INFO *Info);

typedef enum
{
  AcAcpiPsciNone,
  AcAcpiPsciSmc,
  AcAcpiPsciHvc
} AC_ACPI_PSCI_CONDUIT;

typedef struct
{
  UINT16               ArmBootArch;
  AC_ACPI_PSCI_CONDUIT PsciConduit;
  BOOLEAN              HardwareReducedAcpi;
} AC_ACPI_FADT_INFO;

/**
  Copy ARM boot flags from a unique FADT (signature FACP), revision 5.1 or newer.

  Only the prefix through MinorVersion is required and interpreted.
  PsciConduit is None when PSCI_COMPLIANT is clear, regardless of PSCI_USE_HVC.
  Raw ArmBootArch is preserved. No PSCI call, reset-register access, or
  DSDT/FACS traversal is performed. Info has no allocations or borrowed pointers.
  Failure clears Info; errors and Boot Services requirements follow
  AcAcpiReadGtdt().
**/
EFI_STATUS AcAcpiReadFadt(IN CONST AC_ACPI_CONTEXT *Context, OUT AC_ACPI_FADT_INFO *Info);

#endif
