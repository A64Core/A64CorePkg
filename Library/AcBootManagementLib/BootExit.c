/** @file
  Final memory map validation and the Boot Services commit boundary.
**/

#include "BootManagementInternal.h"

#define AC_EXIT_MAP_LIMIT (16U * 1024U * 1024U)
#define AC_EXIT_ATTEMPTS 8U

EFI_STATUS
InternalPrepareBootExit(IN OUT AC_BOOT_KERNEL *Kernel)
{
  UINTN      Size = 0;
  UINTN      Stride = 0;
  UINTN      Key;
  UINT32     Version;
  EFI_STATUS Status;

  Status = gBS->GetMemoryMap(&Size, NULL, &Key, &Stride, &Version);
  if (Status != EFI_BUFFER_TOO_SMALL)
    return EFI_ERROR(Status) ? Status : EFI_COMPROMISED_DATA;
  if (Size == 0 || Stride < sizeof(EFI_MEMORY_DESCRIPTOR))
    return EFI_COMPROMISED_DATA;
  if (Size > AC_EXIT_MAP_LIMIT / 2 || Stride > (AC_EXIT_MAP_LIMIT - 2 * Size) / 64)
    return EFI_BAD_BUFFER_SIZE;
  Kernel->ExitMapCapacity = MAX(64U * 1024U, 2 * Size + 64 * Stride);
  Kernel->ExitMap = AllocatePool(Kernel->ExitMapCapacity);
  return Kernel->ExitMap == NULL ? EFI_OUT_OF_RESOURCES : EFI_SUCCESS;
}

EFI_STATUS
InternalStartBootKernel(IN EFI_HANDLE ImageHandle, IN OUT AC_BOOT_KERNEL *Kernel)
{
  AC_APPLE_BOOT_INFO Info;
  EFI_STATUS         Status;
  UINTN              Size;
  UINTN              Key;
  UINTN              Stride;
  UINT32             Version;
  BOOLEAN            AttemptedExit = FALSE;

  if (ImageHandle == NULL || Kernel == NULL || Kernel->CpuContext == NULL ||
      Kernel->ExitMap == NULL)
    return EFI_INVALID_PARAMETER;
  Kernel->Stage = L"Validate final handoff";
  Status = AcAppleBootGetInfo(Kernel->BootContext, &Info);
  if (EFI_ERROR(Status))
    return Status;
  for (UINTN Attempt = 0; Attempt < AC_EXIT_ATTEMPTS; ++Attempt)
  {
    Size = Kernel->ExitMapCapacity;
    Status = gBS->GetMemoryMap(&Size, Kernel->ExitMap, &Key, &Stride, &Version);
    if (!EFI_ERROR(Status) && Size > Kernel->ExitMapCapacity)
      Status = EFI_COMPROMISED_DATA;
    if (!EFI_ERROR(Status))
      Status =
        AcAppleBootValidateHandoff(Kernel->BootContext, Kernel->ExitMap, Size, Stride, Version);
    if (EFI_ERROR(Status))
    {
      if (!AttemptedExit)
        return Status;
      AcCpuHandoffAbort(Kernel->CpuContext, Status);
    }
    Kernel->Stage = L"Exit Boot Services";
    AttemptedExit = TRUE;
    Status = gBS->ExitBootServices(ImageHandle, Key);
    if (!EFI_ERROR(Status))
    {
      AcCpuHandoffEnter(Kernel->CpuContext, Info.Kernel.EntryPoint, Info.BootArgs.PhysicalAddress,
                        &Info.Allocation);
    }
    if (Status != EFI_INVALID_PARAMETER)
    {
      AcCpuHandoffAbort(Kernel->CpuContext, Status);
    }
  }
  AcCpuHandoffAbort(Kernel->CpuContext, EFI_ABORTED);
}
