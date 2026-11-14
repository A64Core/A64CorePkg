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

VOID InternalConfigurationFreePatch(IN OUT AC_KERNEL_PATCH_ENTRY *Patch);

EFI_STATUS InternalConfigurationParseKernel(IN CONST AC_PLIST_NODE  *Node,
                                            IN OUT AC_KERNEL_CONFIG *Kernel,
                                            IN OUT UINTN            *IssueCount);

VOID       InternalConfigurationFreeNvram(IN OUT AC_NVRAM_CONFIG *Nvram);
EFI_STATUS InternalConfigurationParseNvram(IN CONST AC_PLIST_NODE *Node,
                                           IN OUT AC_NVRAM_CONFIG *Nvram, IN OUT UINTN *IssueCount);

EFI_STATUS InternalConfigurationParsePlatform(IN CONST AC_PLIST_NODE    *Node,
                                              IN OUT AC_PLATFORM_CONFIG *Platform,
                                              IN OUT UINTN              *IssueCount);

EFI_STATUS InternalConfigurationParseUefi(IN CONST AC_PLIST_NODE *Node, IN OUT AC_UEFI_CONFIG *Uefi,
                                          IN OUT UINTN *IssueCount);

#endif
