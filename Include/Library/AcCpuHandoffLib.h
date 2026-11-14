/** @file
  Configured EL1 CPU handoff and terminal failure handling.
**/

#ifndef AC_CPU_HANDOFF_LIB_H
#define AC_CPU_HANDOFF_LIB_H

#include <Uefi.h>
#include <IndustryStandard/AppleBoot.h>
#include <Library/AcConfigurationLib.h>

typedef struct AC_CPU_HANDOFF_CONTEXT AC_CPU_HANDOFF_CONTEXT;

/**
  Allocate an independent stack, transition code and mode-specific resources.

  Both modes require an EL1 caller. Mode is executed as configured without
  capability tests or fallback. SecureLaunch reads TCBPath relative to EFI\AC
  on ImageHandle's source file system; Direct does not open that file system.
  Video is borrowed for this call and may be NULL. No launch occurs here.
  Failure clears Context and releases all resources. Requires Boot Services.
**/
EFI_STATUS
AcCpuHandoffPrepare(IN EFI_HANDLE ImageHandle, IN CONST AC_CPU_HANDOFF_CONFIG *Config,
                    IN CONST AC_APPLE_BOOT_VIDEO *Video OPTIONAL,
                    OUT AC_CPU_HANDOFF_CONTEXT        **Context);

/** Release a prepared context before any ExitBootServices attempt. NULL is allowed. **/
VOID AcCpuHandoffFree(IN AC_CPU_HANDOFF_CONTEXT *Context OPTIONAL);

/**
  Publish resident resources and enter PhysicalEntry at EL1 with x0 = Argument.

  Call only after successful ExitBootServices. All addresses are physical and
  must be identity mapped on entry. ResidentMemory covers the loaded kernel and
  its referenced boot data. Interrupts, MMU and caches are disabled at entry.
  SecureLaunch authenticates and launches its prepared TCB here. Never returns;
  failure records diagnostics and halts without invoking firmware services.
**/
NORETURN VOID EFIAPI AcCpuHandoffEnter(IN AC_CPU_HANDOFF_CONTEXT           *Context,
                                       IN EFI_PHYSICAL_ADDRESS              PhysicalEntry,
                                       IN EFI_PHYSICAL_ADDRESS              Argument,
                                       IN CONST AC_APPLE_BOOT_MEMORY_RANGE *ResidentMemory);

/** Terminal EL1 failure after the first EBS attempt; no firmware calls or cleanup. **/
NORETURN VOID EFIAPI AcCpuHandoffAbort(IN AC_CPU_HANDOFF_CONTEXT *Context, IN EFI_STATUS Status);

#endif
