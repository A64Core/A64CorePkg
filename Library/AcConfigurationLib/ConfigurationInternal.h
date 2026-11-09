/** @file
  Configuration mapping helpers.
**/

#ifndef AC_CONFIGURATION_INTERNAL_H
#define AC_CONFIGURATION_INTERNAL_H

#include <Library/AcConfigurationLib.h>
#include <Library/AcPlistLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>

VOID InternalConfigurationIssue(IN OUT UINTN *IssueCount, IN CONST CHAR8 *Scope, IN UINTN Index,
                                IN CONST CHAR8 *Field, IN CONST CHAR8 *Reason);

VOID InternalConfigurationFreeDriver(IN OUT AC_UEFI_DRIVER_ENTRY *Driver);

EFI_STATUS InternalConfigurationParseUefi(IN CONST AC_PLIST_NODE *Node, IN OUT AC_UEFI_CONFIG *Uefi,
                                          IN OUT UINTN *IssueCount);

#endif
