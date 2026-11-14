/** @file
  EDK II preparation of the Slbounce launch protocol.

  Adapted from Slbounce by Nikita Travkin; see LICENSE.slbounce.
  Protocol declarations come from the pinned ThirdParty/slbounce submodule.
**/

#include "HandoffInternal.h"

#include <Guid/FileInfo.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <sl.h>

#define AC_TCB_FILE_LIMIT (16U * 1024U * 1024U)
#define AC_TCB_IMAGE_PAGES 512U
#define AC_TCB_IMAGE_SIZE EFI_PAGES_TO_SIZE(AC_TCB_IMAGE_PAGES)

STATIC_ASSERT(sizeof(struct sl_smc_params) == 48, "Unexpected launch parameter ABI");
STATIC_ASSERT(sizeof(struct sl_tz_data) == 0xC8, "Unexpected transition ABI");

STATIC EFI_STATUS
AllocateLaunchPages(UINTN Pages, EFI_PHYSICAL_ADDRESS *Address)
{
  EFI_PHYSICAL_ADDRESS Result = 0;
  EFI_STATUS           Status = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, Pages, &Result);

  if (!EFI_ERROR(Status))
  {
    *Address = Result;
    ZeroMem((VOID *)(UINTN)Result, EFI_PAGES_TO_SIZE(Pages));
  }
  return Status;
}

STATIC EFI_STATUS
OpenTcb(EFI_HANDLE ImageHandle, CONST CHAR8 *Path, EFI_FILE_PROTOCOL **File)
{
  EFI_LOADED_IMAGE_PROTOCOL       *Image;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
  EFI_FILE_PROTOCOL               *Root;
  CHAR16                           FullPath[1033] = L"\\EFI\\AC\\";
  UINTN                            Length;
  UINTN                            Component = 0;
  EFI_STATUS                       Status;

  *File = NULL;
  if (ImageHandle == NULL || Path == NULL)
    return EFI_INVALID_PARAMETER;
  Length = AsciiStrnLenS(Path, 1025);
  if (Length == 0 || Length > 1024)
    return EFI_INVALID_PARAMETER;
  for (UINTN Index = 0; Index <= Length; ++Index)
  {
    CHAR8 Ch = Path[Index];
    if (Ch == '\0' || Ch == '\\' || Ch == '/')
    {
      UINTN Size = Index - Component;
      if (Size == 0 || (Size == 1 && Path[Component] == '.') ||
          (Size == 2 && Path[Component] == '.' && Path[Component + 1] == '.'))
        return EFI_INVALID_PARAMETER;
      Component = Index + 1;
    }
    else if (Ch < ' ' || Ch > '~' || Ch == ':')
      return EFI_INVALID_PARAMETER;
    FullPath[8 + Index] = Ch == '/' ? L'\\' : (CHAR16)Ch;
  }
  Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Image);
  if (EFI_ERROR(Status))
    return Status;
  if (Image->DeviceHandle == NULL)
    return EFI_NOT_FOUND;
  Status = gBS->HandleProtocol(Image->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid,
                               (VOID **)&FileSystem);
  if (EFI_ERROR(Status))
    return Status;
  Status = FileSystem->OpenVolume(FileSystem, &Root);
  if (EFI_ERROR(Status))
    return Status;
  Status = Root->Open(Root, File, FullPath, EFI_FILE_MODE_READ, 0);
  Root->Close(Root);
  return Status;
}

STATIC EFI_STATUS
ReadTcb(EFI_FILE_PROTOCOL *File, VOID **Buffer, UINTN *Size)
{
  EFI_FILE_INFO *Info;
  UINTN          InfoSize = 0;
  UINTN          Capacity;
  UINTN          Offset;
  UINTN          Read;
  EFI_STATUS     Status;

  *Buffer = NULL;
  *Size = 0;
  Status = File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL)
    return EFI_ERROR(Status) ? Status : EFI_COMPROMISED_DATA;
  if (InfoSize < SIZE_OF_EFI_FILE_INFO + sizeof(CHAR16) || InfoSize > 64U * 1024U)
    return EFI_COMPROMISED_DATA;
  Capacity = InfoSize;
  Info = AllocatePool(InfoSize);
  if (Info == NULL)
    return EFI_OUT_OF_RESOURCES;
  Status = File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, Info);
  if (!EFI_ERROR(Status))
  {
    if (InfoSize > Capacity || InfoSize < SIZE_OF_EFI_FILE_INFO + sizeof(CHAR16) ||
        Info->Size < SIZE_OF_EFI_FILE_INFO + sizeof(CHAR16) || Info->Size > InfoSize ||
        (Info->Attribute & EFI_FILE_DIRECTORY) != 0)
      Status = EFI_COMPROMISED_DATA;
    else if (Info->FileSize < 64 || Info->FileSize > AC_TCB_FILE_LIMIT)
      Status = EFI_BAD_BUFFER_SIZE;
    else
      *Size = (UINTN)Info->FileSize;
  }
  FreePool(Info);
  if (EFI_ERROR(Status))
    return Status;
  *Buffer = AllocatePool(*Size);
  if (*Buffer == NULL)
    return EFI_OUT_OF_RESOURCES;
  for (Offset = 0; Offset < *Size; Offset += Read)
  {
    Read = *Size - Offset;
    Status = File->Read(File, &Read, (UINT8 *)*Buffer + Offset);
    if (!EFI_ERROR(Status) && (Read == 0 || Read > *Size - Offset))
      Status = EFI_DEVICE_ERROR;
    if (EFI_ERROR(Status))
      break;
  }
  if (EFI_ERROR(Status))
  {
    FreePool(*Buffer);
    *Buffer = NULL;
  }
  return Status;
}

STATIC UINT16
Pe16(CONST UINT8 *Data, UINTN Offset)
{
  return ReadUnaligned16((CONST UINT16 *)(Data + Offset));
}

STATIC UINT32
Pe32(CONST UINT8 *Data, UINTN Offset)
{
  return ReadUnaligned32((CONST UINT32 *)(Data + Offset));
}

STATIC EFI_STATUS
ValidateTcb(CONST UINT8 *Data, UINTN Size, UINT32 *NtOffset, UINT32 *Certificate,
            UINT32 *CertificateSize)
{
  CONST UINT8 *Nt;
  CONST UINT8 *Section;
  CONST UINT8 *Other;
  UINT32       Headers;
  UINT32       ImageSize;
  UINT32       Entry;
  UINT32       Raw;
  UINT32       RawSize;
  UINT32       Virtual;
  UINT32       Span;
  UINT32       OtherSpan;
  UINT16       Count;
  BOOLEAN      ExecutableEntry = FALSE;

  if (Size < 64 || Size > AC_TCB_FILE_LIMIT || Pe16(Data, 0) != 0x5A4D)
    return EFI_COMPROMISED_DATA;
  *NtOffset = Pe32(Data, 0x3C);
  if (*NtOffset < 64 || *NtOffset > Size || Size - *NtOffset < 264)
    return EFI_COMPROMISED_DATA;
  Nt = Data + *NtOffset;
  if (Pe32(Nt, 0) != 0x4550 || Pe16(Nt, 4) != 0xAA64 || Pe16(Nt, 20) != 240 ||
      Pe16(Nt, 24) != 0x20B || Pe16(Nt, 92) != 16 || Pe32(Nt, 132) != 16)
    return EFI_UNSUPPORTED;
  Count = Pe16(Nt, 6);
  Headers = Pe32(Nt, 84);
  ImageSize = Pe32(Nt, 80);
  Entry = Pe32(Nt, 40);
  *Certificate = Pe32(Nt, 168);
  *CertificateSize = Pe32(Nt, 172);
  if (Count == 0 || Count > 96 || ImageSize == 0 || ImageSize > AC_TCB_IMAGE_SIZE ||
      Entry >= ImageSize || Headers > ImageSize || Headers > Size ||
      (UINT64)*NtOffset + 264 + (UINT64)Count * 40 > Headers || *Certificate < Headers ||
      (*Certificate & 7) != 0 || *Certificate > Size || *CertificateSize < 8 ||
      *CertificateSize > Size - *Certificate)
    return EFI_COMPROMISED_DATA;
  if (Pe32(Data + *Certificate, 0) < 8 || Pe32(Data + *Certificate, 0) > *CertificateSize ||
      Pe16(Data + *Certificate, 4) != 0x200 || Pe16(Data + *Certificate, 6) != 2)
    return EFI_COMPROMISED_DATA;
  for (UINTN Index = 0; Index < Count; ++Index)
  {
    Section = Nt + 264 + Index * 40;
    Raw = Pe32(Section, 20);
    RawSize = Pe32(Section, 16);
    Virtual = Pe32(Section, 12);
    Span = MAX(Pe32(Section, 8), RawSize);
    if ((RawSize != 0 && (Raw < Headers || Raw > *Certificate || RawSize > *Certificate - Raw)) ||
        (Span != 0 && (Virtual < Headers || Virtual > ImageSize || Span > ImageSize - Virtual)))
      return EFI_COMPROMISED_DATA;
    for (UINTN Previous = 0; Previous < Index; ++Previous)
    {
      Other = Nt + 264 + Previous * 40;
      OtherSpan = MAX(Pe32(Other, 8), Pe32(Other, 16));
      if ((Span != 0 && OtherSpan != 0 && Virtual < (UINT64)Pe32(Other, 12) + OtherSpan &&
           Pe32(Other, 12) < (UINT64)Virtual + Span) ||
          (RawSize != 0 && Pe32(Other, 16) != 0 &&
           Raw < (UINT64)Pe32(Other, 20) + Pe32(Other, 16) &&
           Pe32(Other, 20) < (UINT64)Raw + RawSize))
        return EFI_COMPROMISED_DATA;
    }
    if ((Pe32(Section, 36) & BIT29) != 0 && Entry >= Virtual && Entry - Virtual < Span)
      ExecutableEntry = TRUE;
  }
  return ExecutableEntry ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
}

VOID
InternalFreeSecureLaunch(AC_SECURE_LAUNCH *Launch)
{
  if (Launch->BootParameters != 0)
    (VOID) gBS->FreePages(Launch->BootParameters, 3);
  if (Launch->Shared != 0)
    (VOID) gBS->FreePages(Launch->Shared, Launch->SharedPages);
  if (Launch->Image != 0)
    (VOID) gBS->FreePages(Launch->Image, Launch->ImagePages);
  ZeroMem(Launch, sizeof(*Launch));
}

EFI_STATUS
InternalPrepareSecureLaunch(EFI_HANDLE ImageHandle, CONST CHAR8 *Path,
                            AC_CPU_HANDOFF_CONTEXT *Context)
{
  AC_SECURE_LAUNCH  *Launch = &Context->SecureLaunch;
  EFI_FILE_PROTOCOL *File;
  VOID              *Buffer = NULL;
  UINTN              Size;
  UINT32             NtOffset;
  UINT32             Certificate;
  UINT32             CertificateSize;
  CONST UINT8       *Nt;
  CONST UINT8       *Section;
  struct sl_tz_data *Transition;
  EFI_STATUS         Status;

  Status = OpenTcb(ImageHandle, Path, &File);
  if (EFI_ERROR(Status))
    return Status;
  Status = ReadTcb(File, &Buffer, &Size);
  File->Close(File);
  if (EFI_ERROR(Status))
    return Status;
  Status = ValidateTcb(Buffer, Size, &NtOffset, &Certificate, &CertificateSize);
  if (EFI_ERROR(Status))
    goto Done;
  Launch->ImagePages = AC_TCB_IMAGE_PAGES;
  Status = AllocateLaunchPages(Launch->ImagePages, &Launch->Image);
  if (EFI_ERROR(Status))
    goto Done;
  Nt = (UINT8 *)Buffer + NtOffset;
  CopyMem((VOID *)(UINTN)Launch->Image, Buffer, Pe32(Nt, 84));
  for (UINTN Index = 0; Index < Pe16(Nt, 6); ++Index)
  {
    Section = Nt + 264 + Index * 40;
    if (Pe32(Section, 16) != 0)
      CopyMem((VOID *)(UINTN)(Launch->Image + Pe32(Section, 12)),
              (UINT8 *)Buffer + Pe32(Section, 20), Pe32(Section, 16));
  }
  Launch->SharedPages = 30 + CertificateSize / EFI_PAGE_SIZE + 1;
  Status = AllocateLaunchPages(Launch->SharedPages, &Launch->Shared);
  if (EFI_ERROR(Status))
    goto Done;
  Transition = (VOID *)(UINTN)(Launch->Shared + 2 * EFI_PAGE_SIZE);
  Transition->version = 1;
  Transition->cert_offt = 25 * EFI_PAGE_SIZE;
  Transition->cert_size = CertificateSize;
  CopyMem((UINT8 *)Transition + Transition->cert_offt, (UINT8 *)Buffer + Certificate,
          CertificateSize);
  Transition->tcg_offt = Transition->cert_offt + CertificateSize;
  Transition->tcg_size = 2 * EFI_PAGE_SIZE;
  Transition->tcg_ver = 2;
  Transition->this_size = EFI_PAGES_TO_SIZE(Launch->SharedPages - 3);
  Transition->this_phys = (UINTN)Transition;
  Transition->crt_offt = EFI_PAGE_SIZE;
  Transition->crt_pages_cnt = 24;
  Transition->tb_entry_point = Context->Code;
  Transition->tb_virt = Context->Code;
  Transition->tb_phys = Context->Code;
  Transition->tb_size = EFI_PAGES_TO_SIZE(AC_HANDOFF_CODE_PAGES);
  Transition->tb_data.mair = (UINTN)Context;
  Status = AllocateLaunchPages(3, &Launch->BootParameters);
  if (EFI_ERROR(Status))
    goto Done;
  SetMem((VOID *)(UINTN)Launch->BootParameters, 3 * EFI_PAGE_SIZE, 0xFF);
  Transition->boot_params = Launch->BootParameters;
  Transition->boot_params_size = 3 * EFI_PAGE_SIZE;

Done:
  FreePool(Buffer);
  if (EFI_ERROR(Status))
    InternalFreeSecureLaunch(Launch);
  return Status;
}

STATIC UINT64
ExecuteCommand(AC_CPU_HANDOFF_CONTEXT *Context, enum sl_cmd Command)
{
  AC_SECURE_LAUNCH     *Launch = &Context->SecureLaunch;
  struct sl_smc_params *Parameters = (VOID *)(UINTN)(Launch->Shared + EFI_PAGE_SIZE);

  ZeroMem(Parameters, sizeof(*Parameters));
  Parameters->a = 1;
  Parameters->version = 0x10;
  Parameters->num = Command;
  Parameters->pe_data = Launch->Image;
  Parameters->pe_size = EFI_PAGES_TO_SIZE(Launch->ImagePages);
  Parameters->arg_data = Launch->Shared + 2 * EFI_PAGE_SIZE;
  Parameters->arg_size = EFI_PAGES_TO_SIZE(Launch->SharedPages - 3);
  WriteBackDataCacheRange(Parameters, EFI_PAGE_SIZE);
  WriteBackDataCacheRange(Context, EFI_PAGES_TO_SIZE(AC_HANDOFF_DATA_PAGES));
  return InternalHandoffSmc(SMC_SL_ID, (UINTN)Parameters, Command);
}

VOID EFIAPI
InternalExecuteSecureLaunch(AC_CPU_HANDOFF_CONTEXT *Context)
{
  AC_SECURE_LAUNCH *Launch = &Context->SecureLaunch;
  UINT64            Result;

  Context->Stage = AC_HANDOFF_STAGE_AUTH;
  WriteBackDataCacheRange((VOID *)(UINTN)Launch->Image, EFI_PAGES_TO_SIZE(Launch->ImagePages));
  WriteBackDataCacheRange((VOID *)(UINTN)Launch->Shared, EFI_PAGES_TO_SIZE(Launch->SharedPages));
  WriteBackDataCacheRange((VOID *)(UINTN)Launch->BootParameters, 3 * EFI_PAGE_SIZE);
  Result = ExecuteCommand(Context, SL_CMD_AUTH);
  if (Result != 0)
    AcCpuHandoffAbort(Context, Result);
  InvalidateDataCacheRange((VOID *)(UINTN)(Launch->Shared + EFI_PAGE_SIZE), EFI_PAGE_SIZE);
  Context->Stage = AC_HANDOFF_STAGE_LAUNCH;
  Result = ExecuteCommand(Context, SL_CMD_LAUNCH);
  AcCpuHandoffAbort(Context, Result == 0 ? EFI_ABORTED : Result);
}
