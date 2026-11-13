/** @file
  Owned Apple kernel boot memory preparation.
**/

#include "BootInternal.h"

STATIC BOOLEAN
OverlapsAllocation(CONST AC_APPLE_BOOT_INFO *Info, CONST VOID *Buffer, UINTN Size)
{
  UINTN Start = (UINTN)Buffer;

  return Start > MAX_UINTN - Size ||
         (Start < Info->Allocation.PhysicalAddress + Info->Allocation.Length &&
          Info->Allocation.PhysicalAddress < Start + Size);
}

VOID
AcAppleBootFree(IN AC_APPLE_BOOT_CONTEXT *Context OPTIONAL)
{
  if (Context == NULL)
    return;
  if (Context->PageCount != 0)
    (VOID) gBS->FreePages(Context->Info.Allocation.PhysicalAddress, Context->PageCount);
  if (Context->MemoryMap != NULL)
    FreePool(Context->MemoryMap);
  FreePool(Context);
}

EFI_STATUS
AcAppleBootPrepareMemory(IN CONST AC_APPLE_BOOT_MEMORY_INPUT *Input,
                         OUT AC_APPLE_BOOT_CONTEXT          **Context)
{
  AC_APPLE_BOOT_CONTEXT *Result;
  AC_APPLE_BOOT_INFO    *Info;
  AC_KERNEL_LOAD_INFO    Layout;
  UINTN                  TrustSize;
  UINTN                  TrustCapacity;
  UINTN                  KernelOffset;
  UINTN                  TreeOffset;
  UINTN                  ArgsOffset;
  UINTN                  ResidentSize;
  UINTN                  AllocationOffset;
  UINTN                  Index;
  EFI_STATUS             Status;

  if (Context == NULL)
    return EFI_INVALID_PARAMETER;
  *Context = NULL;
  if (Input == NULL || Input->Kernel == NULL)
    return EFI_INVALID_PARAMETER;
  if (Input->DeviceTreeCapacity > AC_BOOT_MAX_DATA_SIZE)
    return EFI_BAD_BUFFER_SIZE;
  if (gBS == NULL || gBS->GetMemoryMap == NULL || gBS->AllocatePages == NULL ||
      gBS->FreePages == NULL)
    return EFI_UNSUPPORTED;
  Status = AcKernelGetLoadInfo(Input->Kernel, MAX_UINTN, &Layout);
  if (EFI_ERROR(Status))
    return Status;
  Status = InternalBootTrustCacheSize(Input->TrustCaches, Input->TrustCacheCount, &TrustSize);
  if (EFI_ERROR(Status))
    return Status;
  TrustCapacity = ALIGN_VALUE(TrustSize, AC_BOOT_PAGE_SIZE);
  KernelOffset = (UINTN)(Layout.VirtualBase & (Layout.Alignment - 1));
  if (KernelOffset < TrustCapacity)
    KernelOffset += ALIGN_VALUE(TrustCapacity - KernelOffset, Layout.Alignment);
  if (Layout.MemorySize > MAX_UINTN - KernelOffset)
    return EFI_BAD_BUFFER_SIZE;
  TreeOffset = KernelOffset + Layout.MemorySize;
  if (ALIGN_VALUE(Input->DeviceTreeCapacity, AC_BOOT_PAGE_SIZE) > MAX_UINTN - TreeOffset)
    return EFI_BAD_BUFFER_SIZE;
  ArgsOffset = TreeOffset + ALIGN_VALUE(Input->DeviceTreeCapacity, AC_BOOT_PAGE_SIZE);
  if (ArgsOffset > MAX_UINTN - AC_BOOT_PAGE_SIZE || Layout.VirtualBase < KernelOffset)
    return EFI_BAD_BUFFER_SIZE;
  ResidentSize = ArgsOffset + AC_BOOT_PAGE_SIZE;
  AllocationOffset = KernelOffset - TrustCapacity;
  Result = AllocateZeroPool(sizeof(*Result));
  if (Result == NULL)
    return EFI_OUT_OF_RESOURCES;
  Info = &Result->Info;
  Info->VirtualBase = Layout.VirtualBase - KernelOffset;
  Status = InternalBootAllocateResources(AllocationOffset, ResidentSize - AllocationOffset, Result);
  if (EFI_ERROR(Status))
    goto Failure;
  Info->TopOfKernelData = Info->Allocation.PhysicalAddress + Info->Allocation.Length;
  if (OverlapsAllocation(Info, Input->Kernel->Buffer, Input->Kernel->BufferSize))
  {
    Status = EFI_INVALID_PARAMETER;
    goto Failure;
  }
  for (Index = 0; Index < Input->TrustCacheCount; ++Index)
    if (OverlapsAllocation(Info, Input->TrustCaches[Index].Data, Input->TrustCaches[Index].Size))
    {
      Status = EFI_INVALID_PARAMETER;
      goto Failure;
    }
  ZeroMem((VOID *)(UINTN)Info->Allocation.PhysicalAddress, (UINTN)Info->Allocation.Length);
  if (TrustSize != 0)
  {
    Info->TrustCache.PhysicalAddress = Info->Allocation.PhysicalAddress;
    Info->TrustCache.Length = TrustSize;
    InternalBootWriteTrustCaches((VOID *)(UINTN)Info->TrustCache.PhysicalAddress,
                                 Input->TrustCaches, Input->TrustCacheCount);
  }
  Info->DeviceTree.PhysicalAddress = Info->ManagedMemory.PhysicalAddress + TreeOffset;
  Info->DeviceTree.Length = Input->DeviceTreeCapacity;
  Info->BootArgs.PhysicalAddress = Info->ManagedMemory.PhysicalAddress + ArgsOffset;
  Info->BootArgs.Length = sizeof(AC_APPLE_BOOT_ARGS);
  Info->BootArgsVirtual = Info->VirtualBase + ArgsOffset;
  Status = AcKernelLoadCollection(
    Input->Kernel, (VOID *)(UINTN)(Info->Allocation.PhysicalAddress + TrustCapacity),
    Layout.MemorySize, Info->Allocation.PhysicalAddress + TrustCapacity, &Info->Kernel);
  if (EFI_ERROR(Status))
    goto Failure;
  *Context = Result;
  return EFI_SUCCESS;

Failure:
  AcAppleBootFree(Result);
  return Status;
}

EFI_STATUS
AcAppleBootGetInfo(IN CONST AC_APPLE_BOOT_CONTEXT *Context, OUT AC_APPLE_BOOT_INFO *Info)
{
  if (Info == NULL)
    return EFI_INVALID_PARAMETER;
  ZeroMem(Info, sizeof(*Info));
  if (Context == NULL)
    return EFI_INVALID_PARAMETER;
  *Info = Context->Info;
  return EFI_SUCCESS;
}
