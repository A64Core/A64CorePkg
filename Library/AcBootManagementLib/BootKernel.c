/** @file
  Selected kernel file decoding and inspection.
**/

#include "BootManagementInternal.h"

#define AC_BOOT_KERNEL_FILE_LIMIT (512U * 1024U * 1024U)
#define AC_BOOT_KERNEL_DECODED_LIMIT (1024U * 1024U * 1024U)

VOID
InternalFreeBootKernel(IN OUT AC_BOOT_KERNEL *Kernel)
{
  ZeroMem(Kernel, sizeof(*Kernel));
}

EFI_STATUS
InternalLoadBootKernel(IN EFI_HANDLE ImageHandle, IN CONST AC_BOOT_ENTRY *Entry,
                       IN CONST AC_BOOT_RESOURCES *Resources, IN CONST AC_GLOBAL_CONFIG *Config,
                       OUT AC_BOOT_KERNEL *Kernel)
{
  EFI_FILE_PROTOCOL           *Root = NULL;
  EFI_HANDLE                   Handle;
  VOID                        *Buffer = NULL;
  UINTN                        Size;
  UINTN                        Capacity;
  AC_KERNEL_COLLECTION_CONTEXT Collection = {0};
  EFI_STATUS                   Status;

  ZeroMem(Kernel, sizeof(*Kernel));
  if (Config == NULL)
    return EFI_INVALID_PARAMETER;
  Kernel->Stage = L"Open kernel source";
  Status = InternalOpenBootRoot(Entry, &Root, &Handle);
  if (EFI_ERROR(Status))
    return Status;
  Kernel->Stage = L"Read kernel file";
  Status =
    InternalReadBootFile(Root, Resources->KernelPath, AC_BOOT_KERNEL_FILE_LIMIT, &Buffer, &Size);
  Root->Close(Root);
  if (EFI_ERROR(Status))
    return Status;
  Kernel->FileSize = Size;
  Capacity = Size == 0 ? 1 : Size;
  Kernel->Stage = L"Decode kernel payload";
  Status =
    AcKernelDecodePayload(&Buffer, &Size, &Capacity, AC_BOOT_KERNEL_DECODED_LIMIT, &Kernel->Image);
  if (!EFI_ERROR(Status))
  {
    Kernel->BufferSize = Size;
    Kernel->Stage = L"Index kernel collection";
    Status = AcKernelCollectionInit(&Collection, Buffer, Size, Capacity);
  }
  if (!EFI_ERROR(Status))
  {
    Kernel->EntryCount = Collection.EntryCount;
    Kernel->Stage = L"Read Darwin version";
    Status = AcKernelReadDarwinVersion(&Collection, &Kernel->DarwinVersion);
  }
  AcKernelCollectionFree(&Collection);
  FreePool(Buffer);
  if (!EFI_ERROR(Status))
    Kernel->Stage = NULL;
  return Status;
}
