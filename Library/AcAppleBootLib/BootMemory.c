/** @file
  Contiguous RAM selection and resident resource allocation.
**/

#include "BootInternal.h"

STATIC EFI_STATUS
ReadMemoryMap(OUT EFI_MEMORY_DESCRIPTOR **Map, OUT UINTN *Count)
{
  EFI_MEMORY_DESCRIPTOR *Buffer;
  VOID                  *Raw;
  UINTN                  MapSize = 0;
  UINTN                  Stride = 0;
  UINTN                  Capacity;
  UINTN                  EntryCapacity;
  UINTN                  EntryCount;
  UINTN                  Index;
  UINTN                  Key;
  UINTN                  Attempt;
  UINT32                 Version;
  EFI_STATUS             Status;

  *Map = NULL;
  *Count = 0;
  Status = gBS->GetMemoryMap(&MapSize, NULL, &Key, &Stride, &Version);
  if (Status != EFI_BUFFER_TOO_SMALL)
    return EFI_ERROR(Status) ? Status : EFI_COMPROMISED_DATA;
  for (Attempt = 0; Attempt < 8; ++Attempt)
  {
    if (Stride < sizeof(*Buffer) || MapSize == 0)
      return EFI_COMPROMISED_DATA;
    if (Stride > AC_BOOT_MAX_DATA_SIZE / 2 || MapSize > AC_BOOT_MAX_DATA_SIZE - 2 * Stride ||
        MapSize / Stride > 4096)
      return EFI_BAD_BUFFER_SIZE;
    Capacity = MapSize + 2 * Stride;
    EntryCapacity = Capacity / Stride;
    Buffer = AllocatePool(EntryCapacity * sizeof(*Buffer) + Capacity);
    if (Buffer == NULL)
      return EFI_OUT_OF_RESOURCES;
    Raw = Buffer + EntryCapacity;
    MapSize = Capacity;
    Status = gBS->GetMemoryMap(&MapSize, Raw, &Key, &Stride, &Version);
    if (!EFI_ERROR(Status))
    {
      if (MapSize == 0 || MapSize > Capacity || Stride < sizeof(*Buffer) || MapSize % Stride != 0)
        Status = EFI_COMPROMISED_DATA;
      else if (MapSize / Stride > 4096)
        Status = EFI_BAD_BUFFER_SIZE;
      else if (Version != EFI_MEMORY_DESCRIPTOR_VERSION)
        Status = EFI_UNSUPPORTED;
      else if (MapSize / Stride > EntryCapacity)
        Status = EFI_BUFFER_TOO_SMALL;
      else
      {
        EntryCount = MapSize / Stride;
        for (Index = 0; Index < EntryCount; ++Index)
          CopyMem(Buffer + Index, (UINT8 *)Raw + Index * Stride, sizeof(*Buffer));
        *Map = Buffer;
        *Count = EntryCount;
        return EFI_SUCCESS;
      }
    }
    FreePool(Buffer);
    if (Status != EFI_BUFFER_TOO_SMALL)
      return Status;
  }
  return EFI_ABORTED;
}

STATIC BOOLEAN
IsAvailable(CONST EFI_MEMORY_DESCRIPTOR *Descriptor)
{
  // Attribute describes capabilities; WP does not mean the RAM is currently read-only.
  return Descriptor->Type == EfiConventionalMemory &&
         (Descriptor->Attribute & EFI_MEMORY_WB) != 0 &&
         (Descriptor->Attribute & (EFI_MEMORY_RUNTIME | EFI_MEMORY_NV | EFI_MEMORY_SP)) == 0;
}

EFI_STATUS
InternalBootAllocateResources(UINTN Offset, UINTN Size, IN OUT AC_APPLE_BOOT_CONTEXT *Context)
{
  EFI_MEMORY_DESCRIPTOR *Map;
  EFI_MEMORY_DESCRIPTOR  Key;
  AC_APPLE_BOOT_INFO    *Info = &Context->Info;
  UINTN                  Count;
  UINTN                  Index;
  UINTN                  Previous;
  UINT64                 End;
  UINT64                 Length;
  UINT64                 Start;
  EFI_PHYSICAL_ADDRESS   Address;
  EFI_STATUS             Status;

  Status = ReadMemoryMap(&Context->MemoryMap, &Context->MemoryMapCount);
  if (EFI_ERROR(Status))
    return Status;
  Map = Context->MemoryMap;
  Count = Context->MemoryMapCount;
  for (Index = 1; Index < Count; ++Index)
  {
    Key = Map[Index];
    Previous = Index;
    while (Previous > 0 && Map[Previous - 1].PhysicalStart > Key.PhysicalStart)
    {
      Map[Previous] = Map[Previous - 1];
      --Previous;
    }
    Map[Previous] = Key;
  }
  End = 0;
  for (Index = 0; Index < Count; ++Index)
  {
    if ((Map[Index].PhysicalStart & EFI_PAGE_MASK) != 0 || Map[Index].NumberOfPages == 0 ||
        Map[Index].NumberOfPages > (MAX_UINT64 - Map[Index].PhysicalStart) / EFI_PAGE_SIZE ||
        Map[Index].PhysicalStart < End)
      return EFI_COMPROMISED_DATA;
    Length = EFI_PAGES_TO_SIZE(Map[Index].NumberOfPages);
    End = Map[Index].PhysicalStart + Length;
    if (IsAvailable(&Map[Index]))
      Info->AvailableMemory += Length;
  }
  for (Index = 0; Index < Count; ++Index)
  {
    if (!IsAvailable(&Map[Index]))
      continue;
    Start = Map[Index].PhysicalStart;
    End = Start + EFI_PAGES_TO_SIZE(Map[Index].NumberOfPages);
    while (Index + 1 < Count && IsAvailable(&Map[Index + 1]) && Map[Index + 1].PhysicalStart == End)
    {
      ++Index;
      End += EFI_PAGES_TO_SIZE(Map[Index].NumberOfPages);
    }
    if (Start > MAX_UINTN - (AC_BOOT_BLOCK_SIZE - 1))
      continue;
    Start = ALIGN_VALUE(MAX(Start, AC_BOOT_BLOCK_SIZE), AC_BOOT_BLOCK_SIZE);
    End &= ~((UINT64)AC_BOOT_BLOCK_SIZE - 1);
    if (Start >= End)
      continue;
    Length = End - Start;
    if (Length <= Offset || Size >= Length - Offset || Info->VirtualBase > MAX_UINT64 - Length)
      continue;
    if (Length > Info->ManagedMemory.Length)
    {
      Info->ManagedMemory.PhysicalAddress = Start;
      Info->ManagedMemory.Length = Length;
    }
  }
  if (Info->ManagedMemory.Length == 0)
    return EFI_OUT_OF_RESOURCES;
  Address = Info->ManagedMemory.PhysicalAddress + Offset;
  Status = gBS->AllocatePages(AllocateAddress, EfiLoaderData, EFI_SIZE_TO_PAGES(Size), &Address);
  if (!EFI_ERROR(Status))
  {
    Info->Allocation.PhysicalAddress = Address;
    Info->Allocation.Length = Size;
    Context->PageCount = EFI_SIZE_TO_PAGES(Size);
  }
  return Status;
}

STATIC BOOLEAN
ContainsRange(CONST AC_APPLE_BOOT_MEMORY_RANGE *Outer, UINT64 Start, UINT64 Length)
{
  return Length != 0 && Start >= Outer->PhysicalAddress &&
         Start - Outer->PhysicalAddress <= Outer->Length &&
         Length <= Outer->Length - (Start - Outer->PhysicalAddress);
}

EFI_STATUS
AcAppleBootValidateHandoff(IN CONST AC_APPLE_BOOT_CONTEXT *Context, IN CONST VOID *MemoryMap,
                           IN UINTN MemoryMapSize, IN UINTN DescriptorSize,
                           IN UINT32 DescriptorVersion)
{
  CONST AC_APPLE_BOOT_INFO *Info;
  CONST AC_APPLE_BOOT_ARGS *Args;
  EFI_MEMORY_DESCRIPTOR     Descriptor;
  EFI_MEMORY_DESCRIPTOR     Other;
  UINT64                    End;
  UINT64                    OtherEnd;
  UINT64                    Start;
  UINT64                    Limit;
  UINT64                    Covered = 0;
  UINT64                    ManagedEnd;
  UINTN                     Count;

  if (Context == NULL || MemoryMap == NULL || MemoryMapSize == 0 ||
      DescriptorSize < sizeof(Descriptor) || MemoryMapSize % DescriptorSize != 0)
    return EFI_INVALID_PARAMETER;
  if (DescriptorVersion != EFI_MEMORY_DESCRIPTOR_VERSION)
    return EFI_UNSUPPORTED;
  Count = MemoryMapSize / DescriptorSize;
  if (MemoryMapSize > AC_BOOT_MAX_DATA_SIZE || Count > 4096)
    return EFI_BAD_BUFFER_SIZE;
  Info = &Context->Info;
  if (!Info->HasBootArgs)
    return EFI_NOT_READY;
  if (Info->ManagedMemory.Length == 0 ||
      Info->ManagedMemory.PhysicalAddress > MAX_UINT64 - Info->ManagedMemory.Length ||
      Info->VirtualBase > MAX_UINT64 - Info->ManagedMemory.Length ||
      !ContainsRange(&Info->ManagedMemory, Info->Allocation.PhysicalAddress,
                     Info->Allocation.Length) ||
      Context->PageCount != EFI_SIZE_TO_PAGES(Info->Allocation.Length) ||
      (Info->Allocation.PhysicalAddress & EFI_PAGE_MASK) != 0 ||
      (Info->Allocation.Length & EFI_PAGE_MASK) != 0 ||
      !ContainsRange(&Info->Allocation, Info->BootArgs.PhysicalAddress, sizeof(*Args)) ||
      Info->BootArgs.Length != sizeof(*Args) || Info->DeviceTreeSize == 0 ||
      Info->DeviceTreeSize > Info->DeviceTree.Length ||
      !ContainsRange(&Info->Allocation, Info->DeviceTree.PhysicalAddress,
                     Info->DeviceTree.Length) ||
      !ContainsRange(&Info->Allocation, Info->Kernel.PhysicalBase, Info->Kernel.Info.MemorySize) ||
      Info->Kernel.Buffer != (VOID *)(UINTN)Info->Kernel.PhysicalBase ||
      Info->Kernel.Info.EntryPoint < Info->Kernel.Info.VirtualBase ||
      Info->Kernel.Info.EntryPoint - Info->Kernel.Info.VirtualBase >=
        Info->Kernel.Info.MemorySize ||
      Info->Kernel.EntryPoint != Info->Kernel.PhysicalBase +
                                   (Info->Kernel.Info.EntryPoint - Info->Kernel.Info.VirtualBase) ||
      Info->Kernel.Info.VirtualBase !=
        Info->VirtualBase + (Info->Kernel.PhysicalBase - Info->ManagedMemory.PhysicalAddress) ||
      Info->BootArgsVirtual != Info->VirtualBase + (Info->BootArgs.PhysicalAddress -
                                                    Info->ManagedMemory.PhysicalAddress) ||
      Info->TopOfKernelData != Info->Allocation.PhysicalAddress + Info->Allocation.Length ||
      (Info->TrustCache.Length != 0 &&
       !ContainsRange(&Info->Allocation, Info->TrustCache.PhysicalAddress,
                      Info->TrustCache.Length)))
    return EFI_COMPROMISED_DATA;
  Args = (CONST VOID *)(UINTN)Info->BootArgs.PhysicalAddress;
  if (Args->Revision != 2 || Args->Version != 2 || Args->VirtBase != Info->VirtualBase ||
      Args->PhysBase != Info->ManagedMemory.PhysicalAddress ||
      Args->MemSize != Info->ManagedMemory.Length ||
      Args->TopOfKernelData != Info->TopOfKernelData ||
      Args->DeviceTree != Info->VirtualBase + (Info->DeviceTree.PhysicalAddress -
                                               Info->ManagedMemory.PhysicalAddress) ||
      Args->DeviceTreeLength != Info->DeviceTreeSize ||
      AsciiStrnLenS(Args->CommandLine, sizeof(Args->CommandLine)) == sizeof(Args->CommandLine))
    return EFI_COMPROMISED_DATA;
  ManagedEnd = Info->ManagedMemory.PhysicalAddress + Info->ManagedMemory.Length;
  for (UINTN Index = 0; Index < Count; ++Index)
  {
    CopyMem(&Descriptor, (CONST UINT8 *)MemoryMap + Index * DescriptorSize, sizeof(Descriptor));
    if (Descriptor.NumberOfPages == 0 || (Descriptor.PhysicalStart & EFI_PAGE_MASK) != 0 ||
        Descriptor.NumberOfPages > (MAX_UINT64 - Descriptor.PhysicalStart) / EFI_PAGE_SIZE)
      return EFI_COMPROMISED_DATA;
    End = Descriptor.PhysicalStart + EFI_PAGES_TO_SIZE(Descriptor.NumberOfPages);
    for (UINTN Previous = 0; Previous < Index; ++Previous)
    {
      CopyMem(&Other, (CONST UINT8 *)MemoryMap + Previous * DescriptorSize, sizeof(Other));
      OtherEnd = Other.PhysicalStart + EFI_PAGES_TO_SIZE(Other.NumberOfPages);
      if (Descriptor.PhysicalStart < OtherEnd && Other.PhysicalStart < End)
        return EFI_COMPROMISED_DATA;
    }
    Start = MAX(Descriptor.PhysicalStart, Info->ManagedMemory.PhysicalAddress);
    Limit = MIN(End, ManagedEnd);
    if (Start >= Limit)
      continue;
    if ((Descriptor.Attribute & EFI_MEMORY_WB) == 0 ||
        (Descriptor.Attribute & (EFI_MEMORY_RUNTIME | EFI_MEMORY_NV | EFI_MEMORY_SP)) != 0)
      return EFI_ACCESS_DENIED;
    if (Descriptor.Type == EfiLoaderData)
    {
      if (!ContainsRange(&Info->Allocation, Start, Limit - Start))
        return EFI_ACCESS_DENIED;
    }
    else if (Descriptor.Type != EfiConventionalMemory && Descriptor.Type != EfiBootServicesCode &&
             Descriptor.Type != EfiBootServicesData)
      return EFI_ACCESS_DENIED;
    else if (Start < Info->Allocation.PhysicalAddress + Info->Allocation.Length &&
             Info->Allocation.PhysicalAddress < Limit)
      return EFI_ACCESS_DENIED;
    Covered += Limit - Start;
  }
  return Covered == Info->ManagedMemory.Length ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
}
