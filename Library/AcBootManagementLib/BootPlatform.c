/** @file
  Firmware data collection for one Apple kernel launch.
**/

#include "BootManagementInternal.h"

#include <Library/ArmLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/Rng.h>

STATIC EFI_STATUS
ReadCommandLine(CONST AC_APPLE_NVRAM_CONTEXT *Nvram, CHAR8 CommandLine[AC_APPLE_BOOT_LINE_LENGTH])
{
  CONST VOID *Data;
  UINTN       Size;
  UINTN       Length;
  EFI_STATUS  Status;

  Status = AcAppleNvramGetVariable(Nvram, &gAcAppleSystemNvramGuid, "boot-args", &Data, &Size);
  if (Status == EFI_NOT_FOUND)
    Status = AcAppleNvramGetVariable(Nvram, &gAcAppleNvramGuid, "boot-args", &Data, &Size);
  if (Status == EFI_NOT_FOUND)
    return EFI_SUCCESS;
  if (EFI_ERROR(Status))
    return Status;
  if (Size > AC_APPLE_BOOT_LINE_LENGTH)
    return EFI_BAD_BUFFER_SIZE;
  if (Size != 0)
    CopyMem(CommandLine, Data, Size);
  for (Length = 0; Length < Size && CommandLine[Length] != '\0'; ++Length)
    if ((UINT8)CommandLine[Length] < 0x20 || (UINT8)CommandLine[Length] > 0x7E)
      return EFI_COMPROMISED_DATA;
  if (Length == AC_APPLE_BOOT_LINE_LENGTH)
    return EFI_BAD_BUFFER_SIZE;
  for (UINTN Index = Length; Index < Size; ++Index)
    if (CommandLine[Index] != '\0')
      return EFI_COMPROMISED_DATA;
  CommandLine[Length] = '\0';
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
CheckFramebufferRange(UINT64 Address, UINT64 Size)
{
  EFI_MEMORY_DESCRIPTOR *Map;
  EFI_MEMORY_DESCRIPTOR *Descriptor;
  UINTN                  MapSize = 0;
  UINTN                  Capacity;
  UINTN                  Stride;
  UINTN                  Key;
  UINT32                 Version;
  UINT64                 End;
  UINT64                 Covered;
  UINT64                 Next;
  EFI_STATUS             Status;

  if (Address == 0 || Size == 0 || Address > MAX_UINT64 - Size)
    return EFI_INVALID_PARAMETER;
  Status = gBS->GetMemoryMap(&MapSize, NULL, &Key, &Stride, &Version);
  if (Status != EFI_BUFFER_TOO_SMALL)
    return EFI_ERROR(Status) ? Status : EFI_COMPROMISED_DATA;
  for (UINTN Retry = 0; Retry < 8 && Status == EFI_BUFFER_TOO_SMALL; ++Retry)
  {
    if (Stride < sizeof(*Map) || Stride > 16U * 1024U * 1024U / 2 ||
        MapSize > 16U * 1024U * 1024U - 2 * Stride)
      return EFI_COMPROMISED_DATA;
    Capacity = MapSize + 2 * Stride;
    Map = AllocatePool(Capacity);
    if (Map == NULL)
      return EFI_OUT_OF_RESOURCES;
    MapSize = Capacity;
    Status = gBS->GetMemoryMap(&MapSize, Map, &Key, &Stride, &Version);
    if (Status == EFI_BUFFER_TOO_SMALL)
    {
      FreePool(Map);
      continue;
    }
    if (!EFI_ERROR(Status) &&
        (MapSize > Capacity || Stride < sizeof(*Map) || MapSize % Stride != 0 ||
         MapSize / Stride > 4096 || Version != EFI_MEMORY_DESCRIPTOR_VERSION))
      Status = EFI_COMPROMISED_DATA;
    if (!EFI_ERROR(Status))
    {
      End = Address + Size;
      Covered = Address;
      while (Covered < End)
      {
        Next = Covered;
        for (UINTN Offset = 0; Offset < MapSize; Offset += Stride)
        {
          Descriptor = (VOID *)((UINT8 *)Map + Offset);
          if ((Descriptor->PhysicalStart & EFI_PAGE_MASK) != 0 || Descriptor->NumberOfPages == 0 ||
              Descriptor->NumberOfPages > (MAX_UINT64 - Descriptor->PhysicalStart) / EFI_PAGE_SIZE)
          {
            Status = EFI_COMPROMISED_DATA;
            break;
          }
          if (Covered >= Descriptor->PhysicalStart &&
              Covered - Descriptor->PhysicalStart < Descriptor->NumberOfPages * EFI_PAGE_SIZE)
          {
            if (Next != Covered ||
                (Descriptor->Type != EfiReservedMemoryType &&
                 Descriptor->Type != EfiMemoryMappedIO && Descriptor->Type != EfiACPIMemoryNVS))
            {
              Status = EFI_COMPROMISED_DATA;
              break;
            }
            Next = MIN(End, Descriptor->PhysicalStart + Descriptor->NumberOfPages * EFI_PAGE_SIZE);
          }
        }
        if (EFI_ERROR(Status))
          break;
        if (Next == Covered)
        {
          Status = EFI_NOT_FOUND;
          break;
        }
        Covered = Next;
      }
    }
    FreePool(Map);
    return Status;
  }
  return Status == EFI_BUFFER_TOO_SMALL ? EFI_ABORTED : Status;
}

STATIC EFI_STATUS
ReadGop(EFI_HANDLE Handle, AC_APPLE_BOOT_VIDEO *Video, UINT64 *Size)
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL         *Gop;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
  EFI_STATUS                            Status;

  Status = gBS->HandleProtocol(Handle, &gEfiGraphicsOutputProtocolGuid, (VOID **)&Gop);
  if (EFI_ERROR(Status))
    return Status;
  if (Gop == NULL || Gop->Mode == NULL || Gop->Mode->Info == NULL ||
      Gop->Mode->SizeOfInfo < sizeof(*Info) || Gop->Mode->Mode >= Gop->Mode->MaxMode)
    return EFI_COMPROMISED_DATA;
  Info = Gop->Mode->Info;
  if (Info->Version != 0 ||
      (Info->PixelFormat != PixelBlueGreenRedReserved8BitPerColor &&
       (Info->PixelFormat != PixelBitMask || Info->PixelInformation.RedMask != 0x00FF0000 ||
        Info->PixelInformation.GreenMask != 0x0000FF00 ||
        Info->PixelInformation.BlueMask != 0x000000FF ||
        (Info->PixelInformation.ReservedMask != 0 &&
         Info->PixelInformation.ReservedMask != 0xFF000000))))
    return EFI_UNSUPPORTED;
  ZeroMem(Video, sizeof(*Video));
  Video->BaseAddress = Gop->Mode->FrameBufferBase;
  Video->Width = Info->HorizontalResolution;
  Video->Height = Info->VerticalResolution;
  Video->RowBytes = (UINT64)Info->PixelsPerScanLine * 4;
  Video->Depth = 32;
  *Size = Gop->Mode->FrameBufferSize;
  if (Video->BaseAddress == 0 || (Video->BaseAddress & 3) != 0 || Video->Width == 0 ||
      Video->Height == 0 || Video->RowBytes < Video->Width * 4 || *Size == 0 ||
      Video->BaseAddress > MAX_UINT64 - *Size || Video->Height > MAX_UINT64 / Video->RowBytes ||
      Video->RowBytes * Video->Height > *Size)
    return EFI_UNSUPPORTED;
  return CheckFramebufferRange(Video->BaseAddress, *Size);
}

STATIC EFI_STATUS
ReadFramebuffer(CONST AC_FRAMEBUFFER_CONFIG *Config, AC_APPLE_BOOT_VIDEO *Video, UINT64 *Size)
{
  EFI_HANDLE         *Handles;
  UINTN               Count;
  AC_APPLE_BOOT_VIDEO Candidate;
  UINT64              CandidateSize;
  EFI_STATUS          Status;
  BOOLEAN             Found = FALSE;
  BOOLEAN             Ambiguous = FALSE;

  if (Config->Mode == AcFramebufferInvalid)
    return EFI_INVALID_PARAMETER;
  if (Config->Mode == AcFramebufferOverride)
  {
    Video->BaseAddress = Config->Address;
    Video->Width = Config->Width;
    Video->Height = Config->Height;
    Video->RowBytes = Config->RowBytes;
    Video->Depth = 32 | ((UINT64)(Config->Rotation / 90) << AC_APPLE_BOOT_VIDEO_ROTATION_SHIFT);
    *Size = Config->Size;
    return CheckFramebufferRange(Config->Address, Config->Size);
  }
  if (Config->Mode != AcFramebufferAuto)
    return EFI_INVALID_PARAMETER;
  if (gST->ConsoleOutHandle != NULL)
  {
    Status = ReadGop(gST->ConsoleOutHandle, Video, Size);
    if (!EFI_ERROR(Status) || Status == EFI_OUT_OF_RESOURCES)
      return Status;
  }
  Status =
    gBS->LocateHandleBuffer(ByProtocol, &gEfiGraphicsOutputProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR(Status))
    return Status;
  for (UINTN Index = 0; Index < Count; ++Index)
  {
    Status = ReadGop(Handles[Index], &Candidate, &CandidateSize);
    if (Status == EFI_OUT_OF_RESOURCES)
      break;
    if (EFI_ERROR(Status))
      continue;
    if (Found && (CompareMem(Video, &Candidate, sizeof(*Video)) != 0 || *Size != CandidateSize))
      Ambiguous = TRUE;
    *Video = Candidate;
    *Size = CandidateSize;
    Found = TRUE;
  }
  FreePool(Handles);
  if (Status == EFI_OUT_OF_RESOURCES)
    return Status;
  return Ambiguous ? EFI_NO_MAPPING : Found ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

VOID
InternalFreeBootPlatform(IN OUT AC_BOOT_PLATFORM *Platform)
{
  AcAcpiFreeMadt(&Platform->Madt);
  ZeroMem(Platform, sizeof(*Platform));
}

EFI_STATUS
InternalReadBootPlatform(IN CONST AC_FRAMEBUFFER_CONFIG  *Framebuffer,
                         IN CONST AC_APPLE_NVRAM_CONTEXT *Nvram, OUT AC_BOOT_PLATFORM *Platform,
                         OUT CONST CHAR16 **Stage)
{
  AC_ACPI_CONTEXT  *Acpi = NULL;
  EFI_RNG_PROTOCOL *Rng;
  EFI_STATUS        Status;

  ZeroMem(Platform, sizeof(*Platform));
  *Stage = L"Read ACPI CPU information";
  Status = AcAcpiOpen(&Acpi);
  if (!EFI_ERROR(Status))
    Status = AcAcpiReadMadt(Acpi, &Platform->Madt);
  AcAcpiClose(Acpi);
  if (EFI_ERROR(Status))
    goto Failure;
  Platform->BootMpidr = ArmReadMpidr() & 0xFF00FFFFFFULL;
  for (UINTN Index = 0; Index < Platform->Madt.CpuCount; ++Index)
  {
    if ((Platform->Madt.Cpus[Index].Flags & EFI_ACPI_6_5_GIC_ENABLED) == 0)
      continue;
    if (Platform->Madt.Cpus[Index].Mpidr > MAX_UINT32)
    {
      Status = EFI_UNSUPPORTED;
      goto Failure;
    }
    ++Platform->EnabledCpuCount;
  }
  Status = EFI_NOT_FOUND;
  for (UINTN Index = 0; Index < Platform->Madt.CpuCount; ++Index)
    if ((Platform->Madt.Cpus[Index].Flags & EFI_ACPI_6_5_GIC_ENABLED) != 0 &&
        Platform->Madt.Cpus[Index].Mpidr == Platform->BootMpidr)
      Status = EFI_SUCCESS;
  if (EFI_ERROR(Status))
    goto Failure;
  *Stage = L"Read counter frequency";
  Platform->CounterFrequency = (UINT32)ArmReadCntFrq();
  if (Platform->CounterFrequency == 0)
  {
    Status = EFI_UNSUPPORTED;
    goto Failure;
  }
  *Stage = L"Read boot-args variable";
  Status = ReadCommandLine(Nvram, Platform->CommandLine);
  if (EFI_ERROR(Status))
    goto Failure;
  *Stage = L"Read framebuffer information";
  Status = ReadFramebuffer(Framebuffer, &Platform->Video, &Platform->FramebufferSize);
  if (EFI_ERROR(Status))
    goto Failure;
  *Stage = L"Read random seed";
  Status = gBS->LocateProtocol(&gEfiRngProtocolGuid, NULL, (VOID **)&Rng);
  if (!EFI_ERROR(Status))
    Status = Rng == NULL || Rng->GetRNG == NULL
               ? EFI_UNSUPPORTED
               : Rng->GetRNG(Rng, NULL, sizeof(Platform->RandomSeed), Platform->RandomSeed);
  if (EFI_ERROR(Status))
    goto Failure;
  return EFI_SUCCESS;

Failure:
  InternalFreeBootPlatform(Platform);
  return Status;
}
