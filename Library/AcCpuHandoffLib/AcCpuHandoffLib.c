/** @file
  CPU handoff resource ownership and execution.
**/

#include "HandoffInternal.h"

typedef VOID(EFIAPI *AC_HANDOFF_ROUTINE)(AC_CPU_HANDOFF_CONTEXT *Context);
typedef VOID(EFIAPI *AC_HANDOFF_START_ROUTINE)(AC_CPU_HANDOFF_CONTEXT *Context,
                                               AC_HANDOFF_ROUTINE      Continue);

VOID
AcCpuHandoffFree(IN AC_CPU_HANDOFF_CONTEXT *Context OPTIONAL)
{
  if (Context == NULL)
    return;
  if (Context->Code != 0)
    (VOID) gBS->FreePages(Context->Code, AC_HANDOFF_CODE_PAGES);
  (VOID) gBS->FreePages((EFI_PHYSICAL_ADDRESS)(UINTN)Context, AC_HANDOFF_DATA_PAGES);
}

EFI_STATUS
AcCpuHandoffPrepare(IN EFI_HANDLE ImageHandle, IN CONST AC_CPU_HANDOFF_CONFIG *Config,
                    IN CONST AC_APPLE_BOOT_VIDEO *Video OPTIONAL,
                    OUT AC_CPU_HANDOFF_CONTEXT        **Context)
{
  AC_CPU_HANDOFF_CONTEXT *Result;
  EFI_PHYSICAL_ADDRESS    Address = 0;
  UINTN                   CodeSize = (UINTN)AcHandoffCodeEnd - (UINTN)AcHandoffCodeStart;
  EFI_STATUS              Status;

  if (Context == NULL)
    return EFI_INVALID_PARAMETER;
  *Context = NULL;
  if (Config == NULL || Config->Mode != AcCpuHandoffDirect)
    return EFI_INVALID_PARAMETER;
  if (CodeSize == 0 || CodeSize > EFI_PAGES_TO_SIZE(AC_HANDOFF_CODE_PAGES))
    return EFI_BAD_BUFFER_SIZE;
  Status = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, AC_HANDOFF_DATA_PAGES, &Address);
  if (EFI_ERROR(Status))
    return Status;
  Result = (VOID *)(UINTN)Address;
  ZeroMem(Result, EFI_PAGES_TO_SIZE(AC_HANDOFF_DATA_PAGES));
  Result->Mode = Config->Mode;
  Result->Stage = AC_HANDOFF_STAGE_EXIT;
  Result->Stack = Address + EFI_PAGES_TO_SIZE(AC_HANDOFF_DATA_PAGES);
  if (Video != NULL && Video->Width >= 216 && Video->Height >= 96 && (Video->Depth & 0xFF) == 32 &&
      ((Video->BaseAddress & ~AC_APPLE_BOOT_VIDEO_PHYSICAL) & 3) == 0 &&
      (Video->RowBytes & 3) == 0 && Video->Width <= MAX_UINT64 / 4 &&
      Video->RowBytes >= Video->Width * 4 && Video->Height <= MAX_UINT64 / Video->RowBytes &&
      Video->BaseAddress != 0 && Video->BaseAddress <= MAX_UINT64 - Video->Height * Video->RowBytes)
  {
    Result->Frame = Video->BaseAddress & ~AC_APPLE_BOOT_VIDEO_PHYSICAL;
    Result->RowBytes = Video->RowBytes;
  }
  Status = gBS->AllocatePages(AllocateAnyPages, EfiLoaderCode, AC_HANDOFF_CODE_PAGES, &Address);
  if (!EFI_ERROR(Status))
  {
    Result->Code = Address;
    ZeroMem((VOID *)(UINTN)Address, EFI_PAGES_TO_SIZE(AC_HANDOFF_CODE_PAGES));
    CopyMem((VOID *)(UINTN)Address, AcHandoffCodeStart, CodeSize);
    WriteUnaligned64(InternalHandoffCode(Result, AcHandoffContextSlot), (UINTN)Result);
    WriteBackDataCacheRange((VOID *)(UINTN)Address, EFI_PAGES_TO_SIZE(AC_HANDOFF_CODE_PAGES));
    InvalidateInstructionCacheRange((VOID *)(UINTN)Address,
                                    EFI_PAGES_TO_SIZE(AC_HANDOFF_CODE_PAGES));
  }
  if (EFI_ERROR(Status))
  {
    AcCpuHandoffFree(Result);
    return Status;
  }
  *Context = Result;
  return EFI_SUCCESS;
}

NORETURN VOID EFIAPI
AcCpuHandoffAbort(IN AC_CPU_HANDOFF_CONTEXT *Context, IN EFI_STATUS Status)
{
  DisableInterrupts();
  Context->Status = Status;
  ((AC_HANDOFF_ROUTINE)InternalHandoffCode(Context, AcHandoffAbort))(Context);
  for (;;)
    CpuDeadLoop();
}

STATIC VOID EFIAPI
ExecuteHandoff(AC_CPU_HANDOFF_CONTEXT *Context)
{
  ((AC_HANDOFF_ROUTINE)InternalHandoffCode(Context, AcHandoffDirect))(Context);
  AcCpuHandoffAbort(Context, EFI_ABORTED);
}

NORETURN VOID EFIAPI
AcCpuHandoffEnter(IN AC_CPU_HANDOFF_CONTEXT *Context, IN EFI_PHYSICAL_ADDRESS PhysicalEntry,
                  IN EFI_PHYSICAL_ADDRESS              Argument,
                  IN CONST AC_APPLE_BOOT_MEMORY_RANGE *ResidentMemory)
{
  DisableInterrupts();
  Context->Entry = PhysicalEntry;
  Context->Argument = Argument;
  WriteBackDataCacheRange((VOID *)(UINTN)ResidentMemory->PhysicalAddress,
                          (UINTN)ResidentMemory->Length);
  InvalidateInstructionCacheRange((VOID *)(UINTN)ResidentMemory->PhysicalAddress,
                                  (UINTN)ResidentMemory->Length);
  ((AC_HANDOFF_START_ROUTINE)InternalHandoffCode(Context, AcHandoffStart))(Context, ExecuteHandoff);
  AcCpuHandoffAbort(Context, EFI_ABORTED);
}
