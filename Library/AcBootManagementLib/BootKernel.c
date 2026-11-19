/** @file
  Per-selection kernel file preparation and collection loading.
**/

#include "BootManagementInternal.h"
#include <Protocol/LoadedImage.h>

#define AC_BOOT_KERNEL_FILE_LIMIT (512U * 1024U * 1024U)
#define AC_BOOT_KERNEL_DECODED_LIMIT (1024U * 1024U * 1024U)
#define AC_BOOT_TRUST_CACHE_LIMIT (16U * 1024U * 1024U)

STATIC EFI_STATUS
KextFilePath(CONST CHAR8 *Bundle, CONST CHAR8 *Relative, CHAR16 **Result)
{
  CONST CHAR16 Prefix[] = L"\\EFI\\AC\\Kexts\\";
  CONST CHAR8 *Parts[2] = {Bundle, Relative};
  UINTN        Length;
  UINTN        Used;
  UINTN        Part;
  UINTN        Continuations;
  UINTN        Index;
  UINT32       Value;
  UINT32       Minimum;
  UINT8        Byte;
  CONST UINT8 *Text;
  CHAR16      *Path;
  *Result = NULL;
  Length = AsciiStrLen(Bundle) + AsciiStrLen(Relative) + ARRAY_SIZE(Prefix) + 1;
  if (Length > MAX_UINT16 / sizeof(CHAR16))
    return EFI_BAD_BUFFER_SIZE;
  Path = AllocateZeroPool(Length * sizeof(CHAR16));
  if (Path == NULL)
    return EFI_OUT_OF_RESOURCES;
  CopyMem(Path, Prefix, sizeof(Prefix));
  Used = ARRAY_SIZE(Prefix) - 1;
  for (Part = 0; Part < 2; ++Part)
  {
    if (Part != 0)
      Path[Used++] = L'\\';
    Text = (CONST UINT8 *)Parts[Part];
    while (*Text != 0)
    {
      Byte = *Text++;
      if (Byte < 0x80)
      {
        Path[Used++] = Byte == '/' ? L'\\' : Byte;
        continue;
      }
      if (Byte >= 0xC2 && Byte <= 0xDF)
      {
        Continuations = 1;
        Value = Byte & 31U;
        Minimum = 0x80;
      }
      else if (Byte >= 0xE0 && Byte <= 0xEF)
      {
        Continuations = 2;
        Value = Byte & 15U;
        Minimum = 0x800;
      }
      else if (Byte >= 0xF0 && Byte <= 0xF4)
      {
        Continuations = 3;
        Value = Byte & 7U;
        Minimum = 0x10000;
      }
      else
        goto Invalid;
      for (Index = 0; Index < Continuations; ++Index)
      {
        if ((*Text & 0xC0U) != 0x80U)
          goto Invalid;
        Value = (Value << 6) | (*Text++ & 63U);
      }
      if (Value < Minimum || Value > 0x10FFFF || (Value >= 0xD800 && Value <= 0xDFFF))
        goto Invalid;
      if (Value < 0x10000)
        Path[Used++] = (CHAR16)Value;
      else
      {
        Value -= 0x10000;
        Path[Used++] = (CHAR16)(0xD800U | (Value >> 10));
        Path[Used++] = (CHAR16)(0xDC00U | (Value & 1023U));
      }
    }
  }
  Path[Used] = 0;
  *Result = Path;
  return EFI_SUCCESS;
Invalid:
  FreePool(Path);
  return EFI_INVALID_PARAMETER;
}

STATIC EFI_STATUS
InjectKexts(EFI_HANDLE ImageHandle, CONST AC_KERNEL_CONFIG *Config, UINT32 DarwinVersion,
            AC_KERNEL_COLLECTION_CONTEXT *Collection)
{
  EFI_LOADED_IMAGE_PROTOCOL       *Loaded;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
  EFI_FILE_PROTOCOL               *Root = NULL;
  AC_KERNEL_KEXT                  *Kexts = NULL;
  UINTN                           *Indices = NULL;
  AC_KERNEL_COLLECTION_CONTEXT     Result = {0};
  CONST AC_KERNEL_ADD_ENTRY       *Entry;
  CHAR16                          *Path = NULL;
  VOID                            *Data;
  UINTN                            Size;
  UINTN                            Count = 0;
  UINTN                            Index;
  UINTN                            Used = 0;
  UINTN                            Total = 0;
  UINTN                            Failed = MAX_UINTN;
  UINT8                           *Old;
  EFI_STATUS                       Status;
  for (Index = 0; Index < Config->AddCount; ++Index)
    if (Config->Add[Index].Enabled && DarwinVersion >= Config->Add[Index].MinKernel &&
        (Config->Add[Index].MaxKernel == 0 || DarwinVersion <= Config->Add[Index].MaxKernel))
      ++Count;
  if (Count == 0)
    return EFI_SUCCESS;
  Kexts = AllocateZeroPool(Count * sizeof(*Kexts));
  Indices = AllocateZeroPool(Count * sizeof(*Indices));
  if (Kexts == NULL || Indices == NULL)
  {
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }
  Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded);
  if (!EFI_ERROR(Status))
    Status = gBS->HandleProtocol(Loaded->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid,
                                 (VOID **)&FileSystem);
  if (!EFI_ERROR(Status))
    Status = FileSystem->OpenVolume(FileSystem, &Root);
  if (EFI_ERROR(Status))
    goto Done;
  for (Index = 0; Index < Config->AddCount; ++Index)
  {
    Entry = &Config->Add[Index];
    if (!Entry->Enabled || DarwinVersion < Entry->MinKernel ||
        (Entry->MaxKernel != 0 && DarwinVersion > Entry->MaxKernel))
      continue;
    Indices[Used] = Index;
    Failed = Used;
    Kexts[Used].BundlePath = Entry->BundlePath;
    Kexts[Used].ExecutablePath = Entry->ExecutablePath;
    Status = KextFilePath(Entry->BundlePath, Entry->PlistPath, &Path);
    if (!EFI_ERROR(Status))
      Status = InternalReadBootFile(Root, Path, 16U * 1024U * 1024U, &Data, &Size);
    if (Path != NULL)
    {
      FreePool(Path);
      Path = NULL;
    }
    if (EFI_ERROR(Status))
      goto Done;
    Kexts[Used].Plist = Data;
    Kexts[Used].PlistSize = Size;
    Total += Size;
    if (Entry->ExecutablePath != NULL && Entry->ExecutablePath[0] != '\0')
    {
      Status = KextFilePath(Entry->BundlePath, Entry->ExecutablePath, &Path);
      if (!EFI_ERROR(Status))
        Status = InternalReadBootFile(Root, Path, 64U * 1024U * 1024U, &Data, &Size);
      if (Path != NULL)
      {
        FreePool(Path);
        Path = NULL;
      }
      if (EFI_ERROR(Status))
        goto Done;
      Kexts[Used].Executable = Data;
      Kexts[Used].ExecutableSize = Size;
      Total += Size;
    }
    if (Total > 256U * 1024U * 1024U)
    {
      Status = EFI_BAD_BUFFER_SIZE;
      goto Done;
    }
    ++Used;
  }
  Failed = MAX_UINTN;
  Status = AcKernelCollectionInject(Collection, Kexts, Count, &Result, &Failed);
  if (!EFI_ERROR(Status))
  {
    Old = Collection->Buffer;
    AcKernelCollectionFree(Collection);
    *Collection = Result;
    FreePool(Old);
  }
Done:
  if (EFI_ERROR(Status))
    DEBUG((DEBUG_ERROR, "ACBM: Kernel.Add[%u] %a: %r\n",
           Failed < Count && Indices != NULL ? (UINT32)Indices[Failed] : MAX_UINT32,
           Failed < Count && Kexts != NULL && Kexts[Failed].BundlePath != NULL
             ? Kexts[Failed].BundlePath
             : "collection",
           Status));
  if (Root != NULL)
    Root->Close(Root);
  for (Index = 0; Kexts != NULL && Index < Count; ++Index)
  {
    if (Kexts[Index].Plist != NULL)
      FreePool((VOID *)Kexts[Index].Plist);
    if (Kexts[Index].Executable != NULL)
      FreePool((VOID *)Kexts[Index].Executable);
  }
  if (Kexts != NULL)
    FreePool(Kexts);
  if (Indices != NULL)
    FreePool(Indices);
  return Status;
}

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
  AC_APPLE_NVRAM_CONTEXT      *Nvram = NULL;
  AC_APPLE_NVRAM_INFO          NvramInfo;
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
  Kernel->Stage = L"Inject Kernel.Add extensions";
  Status = InjectKexts(ImageHandle, &Config->Kernel, Kernel->DarwinVersion, &Collection);
  Buffer = Collection.Buffer;
  if (EFI_ERROR(Status))
    goto Failure;
  Kernel->BufferSize = Collection.BufferSize;
  Kernel->EntryCount = Collection.EntryCount;
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
  Kernel->Stage = L"Capture Apple NVRAM variables";
  Status = AcAppleNvramCreate(&Nvram);
  if (!EFI_ERROR(Status))
    Status = AcAppleNvramGetInfo(Nvram, &NvramInfo);
  if (EFI_ERROR(Status))
    goto Failure;
  Status =
    InternalReadBootPlatform(&Config->Platform.Framebuffer, Nvram, &Platform, &Kernel->Stage);
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
  Status = InternalCreateBootDeviceTree(Entry, Resources, &Platform, &NvramInfo, &Config->Adt,
                                        Input.TrustCacheCount != 0,
                                        Config->Platform.CpuHandoff.Mode, &DeviceTree);
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
  AcAppleNvramFree(Nvram);
  InternalFreeBootPlatform(&Platform);
  AcKernelCollectionFree(&Collection);
  FreePool(Buffer);
  if (TrustBuffer != NULL)
    FreePool(TrustBuffer);
  Kernel->Stage = NULL;
  return EFI_SUCCESS;

Failure:
  AcAppleAdtFree(DeviceTree);
  AcAppleNvramFree(Nvram);
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
