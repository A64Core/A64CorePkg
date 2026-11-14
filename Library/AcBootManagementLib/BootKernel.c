/** @file
  Per-selection kernel file preparation and collection loading.
**/

#include "BootManagementInternal.h"

#define AC_BOOT_KERNEL_FILE_LIMIT (512U * 1024U * 1024U)
#define AC_BOOT_KERNEL_DECODED_LIMIT (1024U * 1024U * 1024U)
#define AC_BOOT_TRUST_CACHE_LIMIT (16U * 1024U * 1024U)

STATIC EFI_STATUS
VerifyRecoveryManifest(EFI_FILE_PROTOCOL *Root, CONST AC_BOOT_RESOURCES *Resources,
                       CONST VOID *Buffer, UINTN Size)
{
  AC_IMG4_MANIFEST Manifest;
  AC_IMG4_MANIFEST TicketManifest;
  VOID            *Ticket = NULL;
  UINTN            TicketSize;
  UINT8            Hash[48];
  EFI_STATUS       Status;

  Status = AcImg4GetManifest(Buffer, Size, &Manifest);
  if (EFI_ERROR(Status))
    return Status;
  if (!Sha384HashAll(Manifest.Data, Manifest.Size, Hash))
    return EFI_DEVICE_ERROR;
  if (CompareMem(Hash, Resources->BootManifestHash, sizeof(Hash)) != 0)
    return EFI_COMPROMISED_DATA;
  Status =
    InternalReadBootFile(Root, Resources->TicketPath, AC_BOOT_TICKET_LIMIT, &Ticket, &TicketSize);
  if (!EFI_ERROR(Status))
    Status = AcImg4GetManifest(Ticket, TicketSize, &TicketManifest);
  if (!EFI_ERROR(Status) &&
      (TicketManifest.Data != Ticket || TicketManifest.Size != TicketSize ||
       TicketSize != Manifest.Size || CompareMem(Ticket, Manifest.Data, TicketSize) != 0))
    Status = EFI_COMPROMISED_DATA;
  if (Ticket != NULL)
    FreePool(Ticket);
  return Status;
}

STATIC EFI_STATUS
ApplyPatches(IN CONST AC_KERNEL_CONFIG *Config, IN OUT AC_KERNEL_COLLECTION_CONTEXT *Collection,
             IN OUT AC_BOOT_KERNEL *Kernel, IN BOOLEAN KernelPhase)
{
  AC_KERNEL_PATCH              Patch;
  CONST AC_KERNEL_PATCH_ENTRY *Entry;
  UINTN                        Index;
  UINT32                       Applied;
  EFI_STATUS                   Status;

  for (Index = 0; Index < Config->PatchCount; ++Index)
  {
    Entry = &Config->Patches[Index];
    if (!Entry->Enabled || ((AsciiStrCmp(Entry->Identifier, "kernel") == 0) != KernelPhase))
      continue;
    if (Kernel->DarwinVersion < Entry->MinKernel ||
        (Entry->MaxKernel != 0 && Kernel->DarwinVersion > Entry->MaxKernel))
      continue;
    ZeroMem(&Patch, sizeof(Patch));
    Patch.Base = Entry->Base;
    Patch.Comment = Entry->Comment;
    Patch.Find = Entry->Find.Data;
    Patch.Replace = Entry->Replace.Data;
    Patch.Mask = Entry->Mask.Data;
    Patch.ReplaceMask = Entry->ReplaceMask.Data;
    Patch.Size = (UINT32)Entry->Replace.Size;
    Patch.Count = Entry->Count;
    Patch.Skip = Entry->Skip;
    Patch.Limit = Entry->Limit;
    Status = AcKernelCollectionApplyPatch(Collection, Entry->Identifier, &Patch, &Applied);
    Kernel->PatchReplacements += Applied;
    if (EFI_ERROR(Status))
      ++Kernel->PatchFailed;
    else
      ++Kernel->PatchApplied;
    DEBUG((EFI_ERROR(Status) ? DEBUG_WARN : DEBUG_INFO,
           "ACBM: Kernel.Patch[%u] %a: %r, %u replacements\n", (UINT32)Index, Entry->Identifier,
           Status, Applied));
    if (Status == EFI_OUT_OF_RESOURCES || Collection->Entries == NULL)
      return EFI_ERROR(Status) ? Status : EFI_COMPROMISED_DATA;
  }
  return EFI_SUCCESS;
}

VOID
InternalFreeBootKernel(IN OUT AC_BOOT_KERNEL *Kernel)
{
  AcAppleBootFree(Kernel->BootContext);
  AcCpuHandoffFree(Kernel->CpuContext);
  if (Kernel->ExitMap != NULL)
    FreePool(Kernel->ExitMap);
  ZeroMem(Kernel, sizeof(*Kernel));
}

EFI_STATUS
InternalLoadBootKernel(IN EFI_HANDLE ImageHandle, IN CONST AC_BOOT_ENTRY *Entry,
                       IN CONST AC_BOOT_RESOURCES *Resources, IN CONST AC_GLOBAL_CONFIG *Config,
                       OUT AC_BOOT_KERNEL *Kernel)
{
  EFI_FILE_PROTOCOL           *Root = NULL;
  EFI_HANDLE                   Handle;
  EFI_STATUS                   Status;
  CONST CHAR16                *Stage;
  VOID                        *Buffer = NULL;
  VOID                        *TrustBuffer = NULL;
  UINTN                        BufferSize;
  UINTN                        BufferCapacity;
  UINTN                        TrustSize;
  AC_KERNEL_COLLECTION_CONTEXT Collection = {0};
  AC_APPLE_BOOT_MEMORY_INPUT   Input = {0};
  AC_APPLE_BOOT_TRUST_CACHE    TrustCache;
  AC_IMG4_PAYLOAD              Payload;
  AC_BOOT_PLATFORM             Platform = {0};
  AC_APPLE_ADT_TREE           *DeviceTree = NULL;
  AC_APPLE_BOOT_INFO           Info;
  AC_APPLE_BOOT_ARGUMENTS      Arguments = {0};
  UINTN                        DeviceTreeSize = 0;

  ZeroMem(Kernel, sizeof(*Kernel));
  if (Config == NULL)
    return EFI_INVALID_PARAMETER;
  Kernel->Stage = L"Open kernel source";
  Status = InternalOpenBootRoot(Entry, &Root, &Handle);
  if (EFI_ERROR(Status))
    return Status;
  Kernel->Stage = L"Read kernel file";
  Status = InternalReadBootFile(Root, Resources->KernelPath, AC_BOOT_KERNEL_FILE_LIMIT, &Buffer,
                                &BufferSize);
  if (!EFI_ERROR(Status) && Entry->Type == AcBootEntryRecovery)
  {
    Kernel->Stage = L"Verify Recovery kernel manifest";
    Status = VerifyRecoveryManifest(Root, Resources, Buffer, BufferSize);
  }
  if (EFI_ERROR(Status))
    goto Failure;
  Kernel->FileSize = BufferSize;
  BufferCapacity = BufferSize == 0 ? 1 : BufferSize;
  Kernel->Stage = L"Decode kernel payload";
  Status = AcKernelDecodePayload(&Buffer, &BufferSize, &BufferCapacity,
                                 AC_BOOT_KERNEL_DECODED_LIMIT, &Kernel->Image);
  if (EFI_ERROR(Status))
    goto Failure;
  Kernel->Stage = L"Index kernel collection";
  Kernel->BufferSize = BufferSize;
  Status = AcKernelCollectionInit(&Collection, Buffer, BufferSize, BufferCapacity);
  if (EFI_ERROR(Status))
    goto Failure;
  Kernel->Stage = L"Read Darwin version";
  Kernel->EntryCount = Collection.EntryCount;
  Status = AcKernelReadDarwinVersion(&Collection, &Kernel->DarwinVersion);
  if (EFI_ERROR(Status))
    goto Failure;
  Kernel->Stage = L"Apply kernel patches";
  Status = ApplyPatches(&Config->Kernel, &Collection, Kernel, TRUE);
  if (EFI_ERROR(Status))
    goto Failure;
  Kernel->Stage = L"Apply component patches";
  Status = ApplyPatches(&Config->Kernel, &Collection, Kernel, FALSE);
  if (EFI_ERROR(Status))
    goto Failure;
  if (Resources->StaticTrustCachePath != NULL)
  {
    Kernel->Stage = L"Read static trust cache";
    Status = InternalReadBootFile(Root, Resources->StaticTrustCachePath, AC_BOOT_TRUST_CACHE_LIMIT,
                                  &TrustBuffer, &TrustSize);
    if (EFI_ERROR(Status))
      goto Failure;
    Kernel->Stage = L"Decode static trust cache";
    Status = AcImg4Parse(TrustBuffer, TrustSize, &Payload);
    if (EFI_ERROR(Status))
      goto Failure;
    if (AsciiStrCmp(Payload.ComponentType, "trst") != 0 || Payload.HasKeybag ||
        Payload.HasCompression)
    {
      Status = EFI_UNSUPPORTED;
      goto Failure;
    }
    TrustCache.Data = Payload.Data;
    TrustCache.Size = Payload.DataSize;
    Input.TrustCaches = &TrustCache;
    Input.TrustCacheCount = 1;
  }
  Root->Close(Root);
  Root = NULL;
  Status = InternalReadBootPlatform(&Config->Platform.Framebuffer, &Platform, &Kernel->Stage);
  if (EFI_ERROR(Status))
    goto Failure;
  Kernel->Stage = L"Prepare CPU handoff";
  Status = AcCpuHandoffPrepare(ImageHandle, &Config->Platform.CpuHandoff, &Platform.Video,
                               &Kernel->CpuContext);
  if (EFI_ERROR(Status))
    goto Failure;
  Kernel->Stage = L"Prepare final memory map workspace";
  Status = InternalPrepareBootExit(Kernel);
  if (EFI_ERROR(Status))
    goto Failure;
  Kernel->Stage = L"Create Apple Device Tree";
  Status = InternalCreateBootDeviceTree(Entry, Resources, &Platform, Input.TrustCacheCount != 0,
                                        &DeviceTree);
  if (EFI_ERROR(Status))
    goto Failure;
  Status = AcAppleAdtSerialize(DeviceTree, &DeviceTreeSize, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL)
    goto Failure;
  Input.DeviceTreeCapacity = DeviceTreeSize;
  Input.Kernel = &Collection;
  Kernel->Stage = L"Prepare Apple boot memory";
  Status = AcAppleBootPrepareMemory(&Input, &Kernel->BootContext);
  if (EFI_ERROR(Status))
    goto Failure;
  Kernel->Stage = L"Update Apple Device Tree resources";
  Status = AcAppleBootGetInfo(Kernel->BootContext, &Info);
  if (!EFI_ERROR(Status))
    Status = InternalUpdateBootDeviceTree(DeviceTree, &Info);
  if (EFI_ERROR(Status))
    goto Failure;
  Status = AcAppleAdtSerialize(DeviceTree, &DeviceTreeSize, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL)
    goto Failure;
  if (DeviceTreeSize != Input.DeviceTreeCapacity)
  {
    Status = EFI_COMPROMISED_DATA;
    goto Failure;
  }
  Kernel->Stage = L"Encode Apple boot arguments";
  Arguments.CommandLine = Platform.CommandLine;
  Arguments.Video = Platform.Video;
  Arguments.Video.BaseAddress |= AC_APPLE_BOOT_VIDEO_PHYSICAL;
  Status = AcAppleBootWriteArgs(Kernel->BootContext, DeviceTree, &Arguments);
  if (EFI_ERROR(Status))
    goto Failure;
  Kernel->Video = Platform.Video;
  Kernel->BootMpidr = Platform.BootMpidr;
  Kernel->EnabledCpuCount = Platform.EnabledCpuCount;
  Kernel->CounterFrequency = Platform.CounterFrequency;
  CopyMem(Kernel->CommandLine, Platform.CommandLine, sizeof(Kernel->CommandLine));
  AcAppleAdtFree(DeviceTree);
  InternalFreeBootPlatform(&Platform);
  AcKernelCollectionFree(&Collection);
  FreePool(Buffer);
  if (TrustBuffer != NULL)
    FreePool(TrustBuffer);
  Kernel->Stage = NULL;
  return EFI_SUCCESS;

Failure:
  AcAppleAdtFree(DeviceTree);
  InternalFreeBootPlatform(&Platform);
  if (Root != NULL)
    Root->Close(Root);
  AcKernelCollectionFree(&Collection);
  if (Buffer != NULL)
    FreePool(Buffer);
  if (TrustBuffer != NULL)
    FreePool(TrustBuffer);
  Stage = Kernel->Stage;
  InternalFreeBootKernel(Kernel);
  Kernel->Stage = Stage;
  return Status;
}
