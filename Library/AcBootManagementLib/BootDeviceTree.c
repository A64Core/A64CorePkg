/** @file
  Basic Apple Device Tree construction and resource address updates.
**/

#include "BootManagementInternal.h"

STATIC EFI_STATUS
ApplyConfiguration(AC_APPLE_ADT_TREE *Tree, CONST AC_ADT_CONFIG *Config)
{
  CONST VOID *Data;
  UINTN       Size;
  EFI_STATUS  Status;

  for (UINTN Index = 0; Index < Config->DeleteCount; ++Index)
  {
    CONST AC_ADT_DELETE_ENTRY *Entry = &Config->Delete[Index];

    Status = Entry->Property == NULL ? AcAppleAdtDeleteNode(Tree, Entry->Path)
                                     : AcAppleAdtDeleteProperty(Tree, Entry->Path, Entry->Property);
    if (EFI_ERROR(Status) && Status != EFI_NOT_FOUND)
      return Status;
  }
  for (UINTN Index = 0; Index < Config->AddCount; ++Index)
  {
    CONST AC_ADT_ADD_ENTRY *Entry = &Config->Add[Index];

    Status = AcAppleAdtEnsureNode(Tree, Entry->Path);
    if (EFI_ERROR(Status))
      return Status;
    for (UINTN Item = 0; Item < Entry->PropertyCount; ++Item)
    {
      CONST AC_ADT_PROPERTY_ENTRY *Property = &Entry->Properties[Item];

      Status = AcAppleAdtGetProperty(Tree, Entry->Path, Property->Name, &Data, &Size);
      if (Status == EFI_NOT_FOUND)
        Status = AcAppleAdtSetProperty(Tree, Entry->Path, Property->Name, Property->Value.Data,
                                       Property->Value.Size);
      if (EFI_ERROR(Status))
        return Status;
    }
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ReadCpuNumber(CONST AC_APPLE_ADT_TREE *Tree, CONST CHAR8 *Path, CONST CHAR8 *Name, UINT32 *Value)
{
  CONST VOID *Data;
  UINTN       Size;
  EFI_STATUS  Status;

  Status = AcAppleAdtGetProperty(Tree, Path, Name, &Data, &Size);
  if (EFI_ERROR(Status))
    return Status;
  if (Size != sizeof(*Value))
    return EFI_COMPROMISED_DATA;
  CopyMem(Value, Data, sizeof(*Value));
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
CheckCpuText(CONST AC_APPLE_ADT_TREE *Tree, CONST CHAR8 *Path, CONST CHAR8 *Name,
             CONST CHAR8 *Expected)
{
  CONST VOID *Data;
  UINTN       Size;
  EFI_STATUS  Status;

  Status = AcAppleAdtGetProperty(Tree, Path, Name, &Data, &Size);
  if (EFI_ERROR(Status))
    return Status;
  return Size == AsciiStrLen(Expected) + 1 && CompareMem(Data, Expected, Size) == 0
           ? EFI_SUCCESS
           : EFI_COMPROMISED_DATA;
}

STATIC EFI_STATUS
ValidateCpuTopology(CONST AC_APPLE_ADT_TREE *Tree, CONST AC_BOOT_PLATFORM *Platform)
{
  CONST CHAR8 *Name;
  CONST VOID  *Data;
  CONST UINT8 *Bytes;
  BOOLEAN     *Seen;
  CHAR8        ClusterTypes[256] = {0};
  CHAR8        Path[80];
  UINTN        Count = 0;
  UINTN        Match;
  UINTN        Size;
  UINT32       Reg;
  UINT32       CpuId;
  UINT32       Frequency;
  UINT8        Cluster;
  EFI_STATUS   Status;

  if (Platform->EnabledCpuCount == 0 || Platform->Madt.CpuCount == 0)
    return EFI_COMPROMISED_DATA;
  Seen = AllocateZeroPool(Platform->Madt.CpuCount * sizeof(*Seen));
  if (Seen == NULL)
    return EFI_OUT_OF_RESOURCES;
  for (;; ++Count)
  {
    Status = AcAppleAdtGetChildName(Tree, "/cpus", Count, &Name);
    if (Status == EFI_NOT_FOUND)
    {
      Status = Count == Platform->EnabledCpuCount ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
      break;
    }
    if (EFI_ERROR(Status))
      break;
    if (Count >= Platform->EnabledCpuCount)
    {
      Status = EFI_COMPROMISED_DATA;
      break;
    }
    AsciiSPrint(Path, sizeof(Path), "/cpus/%a", Name);
    Status = CheckCpuText(Tree, Path, "device_type", "cpu");
    if (!EFI_ERROR(Status))
      Status = ReadCpuNumber(Tree, Path, "reg", &Reg);
    if (!EFI_ERROR(Status))
      Status = ReadCpuNumber(Tree, Path, "cpu-id", &CpuId);
    if (EFI_ERROR(Status))
      break;
    for (Match = 0; Match < Platform->Madt.CpuCount; ++Match)
      if ((Platform->Madt.Cpus[Match].Flags & EFI_ACPI_6_5_GIC_ENABLED) != 0 &&
          Platform->Madt.Cpus[Match].Mpidr == Reg)
        break;
    if (CpuId != Count || Match == Platform->Madt.CpuCount || Seen[Match])
    {
      Status = EFI_COMPROMISED_DATA;
      break;
    }
    Seen[Match] = TRUE;
    Status = CheckCpuText(Tree, Path, "state", Reg == Platform->BootMpidr ? "running" : "stopped");
    if (!EFI_ERROR(Status))
      Status = ReadCpuNumber(Tree, Path, "timebase-frequency", &Frequency);
    if (EFI_ERROR(Status))
      break;
    if (Frequency == 0 || Frequency != Platform->CounterFrequency)
    {
      Status = EFI_COMPROMISED_DATA;
      break;
    }
    Status = AcAppleAdtGetProperty(Tree, Path, "cluster-type", &Data, &Size);
    if (EFI_ERROR(Status))
      break;
    Bytes = Data;
    if ((Size != 1 && Size != 2 && Size != 4 && Size != 8) ||
        (Bytes[0] != 'E' && Bytes[0] != 'P' && Bytes[0] != 'M'))
    {
      Status = EFI_COMPROMISED_DATA;
      break;
    }
    for (UINTN Index = 1; Index < Size; ++Index)
      if (Bytes[Index] != 0)
        Status = EFI_COMPROMISED_DATA;
    if (EFI_ERROR(Status))
      break;
    Cluster = (UINT8)(Reg >> 8);
    if (ClusterTypes[Cluster] != 0 && ClusterTypes[Cluster] != (CHAR8)Bytes[0])
    {
      Status = EFI_COMPROMISED_DATA;
      break;
    }
    ClusterTypes[Cluster] = (CHAR8)Bytes[0];
  }
  if (EFI_ERROR(Status))
    DEBUG((DEBUG_ERROR, "AC: Invalid CPU topology at child %u: %r\n", (UINT32)Count, Status));
  FreePool(Seen);
  return Status;
}

EFI_STATUS
InternalCreateBootDeviceTree(IN CONST AC_BOOT_ENTRY *Entry, IN CONST AC_BOOT_RESOURCES *Resources,
                             IN CONST AC_BOOT_PLATFORM *Platform, IN CONST AC_ADT_CONFIG *Config,
                             IN BOOLEAN HasTrustCache, OUT AC_APPLE_ADT_TREE **Tree)
{
  AC_APPLE_ADT_TREE         *Result;
  AC_APPLE_BOOT_MEMORY_RANGE Empty = {0};
  UINT64                     Zero = 0;
  UINT32                     Reg;
  UINT32                     CpuId = 0;
  CHAR8                      Path[64];
  CONST CHAR8               *State;
  EFI_STATUS                 Status;

  *Tree = NULL;
  if (Config->Invalid)
    return EFI_INVALID_PARAMETER;
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
    if (!EFI_ERROR(Status))
      Status = AcAppleAdtSetProperty(Result, Path, "cpu-id", &CpuId, sizeof(CpuId));
    ++CpuId;
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
    Status = ApplyConfiguration(Result, Config);
  if (!EFI_ERROR(Status))
    Status = ValidateCpuTopology(Result, Platform);
  if (!EFI_ERROR(Status))
    Status = AcAppleAdtEnsureNode(Result, "/chosen/memory-map");
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
