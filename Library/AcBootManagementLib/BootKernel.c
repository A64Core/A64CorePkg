/** @file
  Selected kernel file decoding and inspection.
**/

#include "BootManagementInternal.h"

#define AC_BOOT_KERNEL_FILE_LIMIT (512U * 1024U * 1024U)
#define AC_BOOT_KERNEL_DECODED_LIMIT (1024U * 1024U * 1024U)

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
  if (!EFI_ERROR(Status) && Entry->Type == AcBootEntryRecovery)
  {
    Kernel->Stage = L"Verify Recovery kernel manifest";
    Status = VerifyRecoveryManifest(Root, Resources, Buffer, Size);
  }
  Root->Close(Root);
  if (EFI_ERROR(Status))
  {
    if (Buffer != NULL)
      FreePool(Buffer);
    return Status;
  }
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
  if (!EFI_ERROR(Status))
  {
    Kernel->Stage = L"Apply kernel patches";
    Status = ApplyPatches(&Config->Kernel, &Collection, Kernel, TRUE);
    if (!EFI_ERROR(Status))
      Status = ApplyPatches(&Config->Kernel, &Collection, Kernel, FALSE);
  }
  AcKernelCollectionFree(&Collection);
  FreePool(Buffer);
  if (!EFI_ERROR(Status))
    Kernel->Stage = NULL;
  return Status;
}
