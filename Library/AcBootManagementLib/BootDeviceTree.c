/** @file
  Basic Apple Device Tree construction and resource address updates.
**/

#include "BootManagementInternal.h"

EFI_STATUS
InternalCreateBootDeviceTree(IN CONST AC_BOOT_ENTRY *Entry, IN CONST AC_BOOT_RESOURCES *Resources,
                             IN CONST AC_BOOT_PLATFORM *Platform, IN BOOLEAN HasTrustCache,
                             OUT AC_APPLE_ADT_TREE **Tree)
{
  AC_APPLE_ADT_TREE         *Result;
  AC_APPLE_BOOT_MEMORY_RANGE Empty = {0};
  UINT64                     Zero = 0;
  UINT32                     Reg;
  CHAR8                      Path[64];
  CONST CHAR8               *State;
  EFI_STATUS                 Status;

  *Tree = NULL;
  Status = AcAppleAdtCreate(&Result);
  if (EFI_ERROR(Status))
    return Status;
  Status = AcAppleAdtEnsureNode(Result, "/cpus");
  if (!EFI_ERROR(Status))
    Status = AcAppleAdtEnsureNode(Result, "/chosen/memory-map");
  for (UINTN Index = 0; !EFI_ERROR(Status) && Index < Platform->Madt.CpuCount; ++Index)
  {
    if ((Platform->Madt.Cpus[Index].Flags & EFI_ACPI_6_5_GIC_ENABLED) == 0)
      continue;
    if (Platform->Madt.Cpus[Index].Mpidr > MAX_UINT32)
    {
      Status = EFI_UNSUPPORTED;
      break;
    }
    Reg = (UINT32)Platform->Madt.Cpus[Index].Mpidr;
    AsciiSPrint(Path, sizeof(Path), "/cpus/cpu@%x", Reg);
    Status = AcAppleAdtEnsureNode(Result, Path);
    if (!EFI_ERROR(Status))
      Status = AcAppleAdtSetProperty(Result, Path, "device_type", "cpu", sizeof("cpu"));
    if (!EFI_ERROR(Status))
      Status = AcAppleAdtSetProperty(Result, Path, "reg", &Reg, sizeof(Reg));
    State = Platform->Madt.Cpus[Index].Mpidr == Platform->BootMpidr ? "running" : "stopped";
    if (!EFI_ERROR(Status))
      Status = AcAppleAdtSetProperty(Result, Path, "state", State, AsciiStrLen(State) + 1);
    if (!EFI_ERROR(Status))
      Status =
        AcAppleAdtSetProperty(Result, Path, "timebase-frequency", &Platform->CounterFrequency,
                              sizeof(Platform->CounterFrequency));
  }
  if (!EFI_ERROR(Status))
    Status = AcAppleAdtSetProperty(Result, "/chosen", "random-seed", Platform->RandomSeed,
                                   sizeof(Platform->RandomSeed));
  if (!EFI_ERROR(Status) && Entry->Type == AcBootEntryRecovery)
    Status =
      AcAppleAdtSetProperty(Result, "/chosen", "boot-manifest-hash", Resources->BootManifestHash,
                            sizeof(Resources->BootManifestHash));
  if (!EFI_ERROR(Status))
    Status = AcAppleAdtSetProperty(Result, "/chosen", "dram-base", &Zero, sizeof(Zero));
  if (!EFI_ERROR(Status))
    Status = AcAppleAdtSetProperty(Result, "/chosen", "dram-size", &Zero, sizeof(Zero));
  if (!EFI_ERROR(Status) && HasTrustCache)
    Status =
      AcAppleAdtSetProperty(Result, "/chosen/memory-map", "TrustCache", &Empty, sizeof(Empty));
  if (EFI_ERROR(Status))
  {
    AcAppleAdtFree(Result);
    return Status;
  }
  *Tree = Result;
  return EFI_SUCCESS;
}

EFI_STATUS
InternalUpdateBootDeviceTree(IN OUT AC_APPLE_ADT_TREE *Tree, IN CONST AC_APPLE_BOOT_INFO *Info)
{
  EFI_STATUS Status;

  // Expose the selected contiguous RAM view, without spanning other banks or holes.
  Status = AcAppleAdtSetProperty(Tree, "/chosen", "dram-base", &Info->ManagedMemory.PhysicalAddress,
                                 sizeof(Info->ManagedMemory.PhysicalAddress));
  if (!EFI_ERROR(Status))
    Status = AcAppleAdtSetProperty(Tree, "/chosen", "dram-size", &Info->ManagedMemory.Length,
                                   sizeof(Info->ManagedMemory.Length));
  if (!EFI_ERROR(Status) && Info->TrustCache.Length != 0)
    Status = AcAppleAdtSetProperty(Tree, "/chosen/memory-map", "TrustCache", &Info->TrustCache,
                                   sizeof(Info->TrustCache));
  return Status;
}
