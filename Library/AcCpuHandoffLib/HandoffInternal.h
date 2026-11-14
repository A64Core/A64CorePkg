/** @file
  Private transition state shared with the position-independent handoff code.
**/

#ifndef HANDOFF_INTERNAL_H
#define HANDOFF_INTERNAL_H

#define AC_HANDOFF_ENTRY 0
#define AC_HANDOFF_ARGUMENT 8
#define AC_HANDOFF_STACK 16
#define AC_HANDOFF_STAGE 24
#define AC_HANDOFF_STATUS 32
#define AC_HANDOFF_ESR 40
#define AC_HANDOFF_ELR 48
#define AC_HANDOFF_FAR 56
#define AC_HANDOFF_EXCEPTION_EL 64
#define AC_HANDOFF_FRAME 72
#define AC_HANDOFF_ROW_BYTES 80

#define AC_HANDOFF_CODE_PAGES 2
#define AC_HANDOFF_DATA_PAGES 9
#define AC_HANDOFF_STAGE_EXIT 1
#define AC_HANDOFF_STAGE_AUTH 2
#define AC_HANDOFF_STAGE_LAUNCH 3
#define AC_HANDOFF_STAGE_EL2 4
#define AC_HANDOFF_STAGE_EL1 5
#define AC_HANDOFF_STAGE_BRANCH 6

#ifndef __ASSEMBLER__

#include <Library/AcCpuHandoffLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

typedef struct
{
  EFI_PHYSICAL_ADDRESS Image;
  UINTN                ImagePages;
  EFI_PHYSICAL_ADDRESS Shared;
  UINTN                SharedPages;
  EFI_PHYSICAL_ADDRESS BootParameters;
} AC_SECURE_LAUNCH;

struct AC_CPU_HANDOFF_CONTEXT
{
  UINT64               Entry;
  UINT64               Argument;
  UINT64               Stack;
  UINT64               Stage;
  UINT64               Status;
  UINT64               Esr;
  UINT64               Elr;
  UINT64               Far;
  UINT64               ExceptionEl;
  UINT64               Frame;
  UINT64               RowBytes;
  AC_CPU_HANDOFF_MODE  Mode;
  EFI_PHYSICAL_ADDRESS Code;
  AC_SECURE_LAUNCH     SecureLaunch;
};

STATIC_ASSERT(sizeof(AC_CPU_HANDOFF_CONTEXT) <= EFI_PAGE_SIZE, "Context overlaps the stack");

#define AC_CHECK_HANDOFF(Field, Offset)                                                            \
  STATIC_ASSERT(OFFSET_OF(AC_CPU_HANDOFF_CONTEXT, Field) == Offset, "Handoff state offset")
AC_CHECK_HANDOFF(Entry, AC_HANDOFF_ENTRY);
AC_CHECK_HANDOFF(Argument, AC_HANDOFF_ARGUMENT);
AC_CHECK_HANDOFF(Stack, AC_HANDOFF_STACK);
AC_CHECK_HANDOFF(Stage, AC_HANDOFF_STAGE);
AC_CHECK_HANDOFF(Status, AC_HANDOFF_STATUS);
AC_CHECK_HANDOFF(Esr, AC_HANDOFF_ESR);
AC_CHECK_HANDOFF(Elr, AC_HANDOFF_ELR);
AC_CHECK_HANDOFF(Far, AC_HANDOFF_FAR);
AC_CHECK_HANDOFF(ExceptionEl, AC_HANDOFF_EXCEPTION_EL);
AC_CHECK_HANDOFF(Frame, AC_HANDOFF_FRAME);
AC_CHECK_HANDOFF(RowBytes, AC_HANDOFF_ROW_BYTES);
#undef AC_CHECK_HANDOFF

extern CONST UINT8 AcHandoffCodeStart[], AcHandoffCodeEnd[], AcHandoffStart[], AcHandoffDirect[];
extern CONST UINT8 AcHandoffAbort[], AcHandoffContextSlot[];

EFI_STATUS    InternalPrepareSecureLaunch(EFI_HANDLE ImageHandle, CONST CHAR8 *Path,
                                          AC_CPU_HANDOFF_CONTEXT *Context);
VOID          InternalFreeSecureLaunch(AC_SECURE_LAUNCH *Launch);
VOID EFIAPI   InternalExecuteSecureLaunch(AC_CPU_HANDOFF_CONTEXT *Context);
UINT64 EFIAPI InternalHandoffSmc(UINT64 Function, UINT64 Parameters, UINT64 Command);

STATIC inline VOID *
InternalHandoffCode(CONST AC_CPU_HANDOFF_CONTEXT *Context, CONST UINT8 *Label)
{
  return (VOID *)(UINTN)(Context->Code + ((UINTN)Label - (UINTN)AcHandoffCodeStart));
}

#endif
#endif
