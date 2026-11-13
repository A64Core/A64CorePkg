/** @file
  ARM64 boot argument encoding and transactional ADT serialization.
**/

#include "BootInternal.h"

STATIC_ASSERT(sizeof(AC_APPLE_BOOT_VIDEO) == 0x30, "Unexpected video ABI");
STATIC_ASSERT(sizeof(AC_APPLE_BOOT_MEMORY_RANGE) == 0x10, "Unexpected range ABI");
STATIC_ASSERT(OFFSET_OF(AC_APPLE_BOOT_ARGS, VirtBase) == 0x08, "Unexpected VirtBase offset");
STATIC_ASSERT(OFFSET_OF(AC_APPLE_BOOT_ARGS, PhysBase) == 0x10, "Unexpected PhysBase offset");
STATIC_ASSERT(OFFSET_OF(AC_APPLE_BOOT_ARGS, MemSize) == 0x18, "Unexpected MemSize offset");
STATIC_ASSERT(OFFSET_OF(AC_APPLE_BOOT_ARGS, TopOfKernelData) == 0x20, "Unexpected data offset");
STATIC_ASSERT(OFFSET_OF(AC_APPLE_BOOT_ARGS, Video) == 0x28, "Unexpected Video offset");
STATIC_ASSERT(OFFSET_OF(AC_APPLE_BOOT_ARGS, MachineType) == 0x58, "Unexpected machine offset");
STATIC_ASSERT(OFFSET_OF(AC_APPLE_BOOT_ARGS, DeviceTree) == 0x60, "Unexpected ADT offset");
STATIC_ASSERT(OFFSET_OF(AC_APPLE_BOOT_ARGS, DeviceTreeLength) == 0x68, "Unexpected length offset");
STATIC_ASSERT(OFFSET_OF(AC_APPLE_BOOT_ARGS, CommandLine) == 0x6C, "Unexpected command offset");
STATIC_ASSERT(OFFSET_OF(AC_APPLE_BOOT_ARGS, BootFlags) == 0x470, "Unexpected flags offset");
STATIC_ASSERT(OFFSET_OF(AC_APPLE_BOOT_ARGS, MemSizeActual) == 0x478, "Unexpected size offset");
STATIC_ASSERT(sizeof(AC_APPLE_BOOT_ARGS) == 0x480, "Unexpected boot_args ABI");

STATIC EFI_STATUS
CheckVideo(CONST AC_APPLE_BOOT_INFO *Info, CONST AC_APPLE_BOOT_VIDEO *Video)
{
  AC_APPLE_BOOT_VIDEO Empty = {0};
  UINT64              Size;
  UINT64              Address = Video->BaseAddress & ~AC_APPLE_BOOT_VIDEO_PHYSICAL;

  if (CompareMem(Video, &Empty, sizeof(Empty)) == 0)
    return EFI_SUCCESS;
  if (Address == 0 || (Address & 3) != 0 || Video->Width == 0 || Video->Height == 0 ||
      Video->Width > MAX_UINT64 / 4 || Video->RowBytes < Video->Width * 4 ||
      Video->Height > MAX_UINT64 / Video->RowBytes || (Video->Depth & 0xFF) != 32 ||
      (Video->Depth >> 32) != 0)
    return EFI_INVALID_PARAMETER;
  Size = Video->Height * Video->RowBytes;
  if (Address > MAX_UINT64 - Size ||
      (Address < Info->ManagedMemory.PhysicalAddress + Info->ManagedMemory.Length &&
       Info->ManagedMemory.PhysicalAddress < Address + Size))
    return EFI_INVALID_PARAMETER;
  return EFI_SUCCESS;
}

EFI_STATUS
AcAppleBootWriteArgs(IN OUT AC_APPLE_BOOT_CONTEXT *Context, IN CONST AC_APPLE_ADT_TREE *DeviceTree,
                     IN CONST AC_APPLE_BOOT_ARGUMENTS *Arguments)
{
  AC_APPLE_BOOT_ARGS  Args;
  AC_APPLE_BOOT_INFO *Info;
  VOID               *Serialized;
  UINTN               Size = 0;
  UINTN               LineSize = 0;
  EFI_STATUS          Status;

  if (Context == NULL || DeviceTree == NULL || Arguments == NULL)
    return EFI_INVALID_PARAMETER;
  if ((Arguments->BootFlags & ~AC_APPLE_BOOT_FLAG_DARK) != 0)
    return EFI_UNSUPPORTED;
  if (Arguments->CommandLine != NULL)
    LineSize = AsciiStrnLenS(Arguments->CommandLine, AC_APPLE_BOOT_LINE_LENGTH);
  if (LineSize == AC_APPLE_BOOT_LINE_LENGTH)
    return EFI_BAD_BUFFER_SIZE;
  Info = &Context->Info;
  Status = CheckVideo(Info, &Arguments->Video);
  if (EFI_ERROR(Status))
    return Status;
  Status = AcAppleAdtSerialize(DeviceTree, &Size, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL)
    return Status;
  if (Size > Info->DeviceTree.Length)
    return EFI_BUFFER_TOO_SMALL;
  Serialized = AllocatePool(Size);
  if (Serialized == NULL)
    return EFI_OUT_OF_RESOURCES;
  Status = AcAppleAdtSerialize(DeviceTree, &Size, Serialized);
  if (!EFI_ERROR(Status))
  {
    ZeroMem(&Args, sizeof(Args));
    Args.Revision = 2;
    Args.Version = 2;
    Args.VirtBase = Info->VirtualBase;
    Args.PhysBase = Info->ManagedMemory.PhysicalAddress;
    Args.MemSize = Info->ManagedMemory.Length;
    Args.TopOfKernelData = Info->TopOfKernelData;
    Args.Video = Arguments->Video;
    Args.MachineType = Arguments->MachineType;
    Args.DeviceTree = Info->VirtualBase + (Info->DeviceTree.PhysicalAddress - Args.PhysBase);
    Args.DeviceTreeLength = (UINT32)Size;
    if (LineSize != 0)
      CopyMem(Args.CommandLine, Arguments->CommandLine, LineSize);
    Args.BootFlags = Arguments->BootFlags;
    Args.MemSizeActual = Arguments->MemSizeActual;
    ZeroMem((VOID *)(UINTN)Info->DeviceTree.PhysicalAddress, (UINTN)Info->DeviceTree.Length);
    CopyMem((VOID *)(UINTN)Info->DeviceTree.PhysicalAddress, Serialized, Size);
    CopyMem((VOID *)(UINTN)Info->BootArgs.PhysicalAddress, &Args, sizeof(Args));
    Info->DeviceTreeSize = Size;
    Info->HasBootArgs = TRUE;
  }
  FreePool(Serialized);
  return Status;
}
