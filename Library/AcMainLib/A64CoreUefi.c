/** @file
  Configured UEFI driver loading and device connection.
**/

#include "MainInternal.h"

#define AC_DRIVER_DIRECTORY L"\\EFI\\AC\\Drivers\\"

STATIC EFI_STATUS
Utf8ToUnicode(CONST CHAR8 *Text, CHAR16 **Result, UINTN *Size)
{
  UINTN   Length;
  UINTN   Position;
  UINTN   Used;
  UINTN   Count;
  UINTN   Index;
  UINT32  Value;
  UINT32  Minimum;
  UINT8   First;
  CHAR16 *Unicode;

  *Result = NULL;
  *Size = 0;
  Length = AsciiStrLen(Text);
  if (Length > MAX_UINT32 / sizeof(CHAR16) - 1)
    return EFI_BAD_BUFFER_SIZE;
  Unicode = AllocatePool((Length + 1) * sizeof(CHAR16));
  if (Unicode == NULL)
    return EFI_OUT_OF_RESOURCES;
  Position = 0;
  Used = 0;
  while (Position < Length)
  {
    First = (UINT8)Text[Position++];
    if (First < 0x80)
    {
      Unicode[Used++] = First;
      continue;
    }
    if ((First >= 0xC2) && (First <= 0xDF))
    {
      Count = 1;
      Value = First & 0x1FU;
      Minimum = 0x80;
    }
    else if ((First >= 0xE0) && (First <= 0xEF))
    {
      Count = 2;
      Value = First & 0x0FU;
      Minimum = 0x800;
    }
    else if ((First >= 0xF0) && (First <= 0xF4))
    {
      Count = 3;
      Value = First & 7U;
      Minimum = 0x10000;
    }
    else
      goto Invalid;
    if (Count > Length - Position)
      goto Invalid;
    for (Index = 0; Index < Count; ++Index)
    {
      if (((UINT8)Text[Position] & 0xC0U) != 0x80U)
        goto Invalid;
      Value = (Value << 6) | ((UINT8)Text[Position++] & 0x3FU);
    }
    if ((Value < Minimum) || (Value > 0x10FFFF) || ((Value >= 0xD800) && (Value <= 0xDFFF)))
      goto Invalid;
    if (Value < 0x10000)
      Unicode[Used++] = (CHAR16)Value;
    else
    {
      Value -= 0x10000;
      Unicode[Used++] = (CHAR16)(0xD800U | (Value >> 10));
      Unicode[Used++] = (CHAR16)(0xDC00U | (Value & 0x3FFU));
    }
  }
  Unicode[Used] = L'\0';
  *Result = Unicode;
  *Size = (Used + 1) * sizeof(CHAR16);
  return EFI_SUCCESS;

Invalid:
  FreePool(Unicode);
  return EFI_INVALID_PARAMETER;
}

STATIC VOID
ReleaseFailedDriver(EFI_HANDLE ImageHandle, CHAR16 *Options)
{
  EFI_LOADED_IMAGE_PROTOCOL *Image;
  EFI_STATUS                 Status;

  if (ImageHandle != NULL)
  {
    Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Image);
    if (!EFI_ERROR(Status))
      Status = gBS->UnloadImage(ImageHandle);
    else if ((Status == EFI_INVALID_PARAMETER) || (Status == EFI_UNSUPPORTED) ||
             (Status == EFI_NOT_FOUND))
      Status = EFI_SUCCESS;
    if (EFI_ERROR(Status))
    {
      InternalMainReport(L"Release failed driver", NULL, Status);
      // A still-loaded image may retain its load options.
      return;
    }
  }
  if (Options != NULL)
    FreePool(Options);
}

STATIC EFI_STATUS
LoadDriver(EFI_HANDLE Parent, EFI_HANDLE Device, CONST AC_UEFI_DRIVER_ENTRY *Driver)
{
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;
  EFI_LOADED_IMAGE_PROTOCOL *Image;
  EFI_HANDLE                 Handle;
  EFI_STATUS                 Status;
  CONST CHAR16              *Stage;
  CHAR16                    *Name;
  CHAR16                    *Path;
  CHAR16                    *Options;
  UINTN                      NameSize;
  UINTN                      PathSize;
  UINTN                      OptionsSize;
  UINTN                      Index;

  Handle = NULL;
  DevicePath = NULL;
  Name = NULL;
  Path = NULL;
  Options = NULL;
  Stage = L"Convert driver path";
  Status = Utf8ToUnicode(Driver->Path, &Name, &NameSize);
  if (EFI_ERROR(Status))
    goto Done;
  Stage = L"Build driver path";
  PathSize = sizeof(AC_DRIVER_DIRECTORY) - sizeof(CHAR16) + NameSize;
  if (PathSize > MAX_UINT16 - SIZE_OF_FILEPATH_DEVICE_PATH)
  {
    Status = EFI_BAD_BUFFER_SIZE;
    goto Done;
  }
  Path = AllocatePool(PathSize);
  if (Path == NULL)
  {
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }
  CopyMem(Path, AC_DRIVER_DIRECTORY, sizeof(AC_DRIVER_DIRECTORY) - sizeof(CHAR16));
  CopyMem((UINT8 *)Path + sizeof(AC_DRIVER_DIRECTORY) - sizeof(CHAR16), Name, NameSize);
  for (Index = 0; Index < PathSize / sizeof(CHAR16); ++Index)
    if (Path[Index] == L'/')
      Path[Index] = L'\\';
  DevicePath = FileDevicePath(Device, Path);
  if (DevicePath == NULL)
  {
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }
  Stage = L"Load driver";
  Status = gBS->LoadImage(FALSE, Parent, DevicePath, NULL, 0, &Handle);
  FreePool(DevicePath);
  DevicePath = NULL;
  if (EFI_ERROR(Status))
    goto Done;
  Stage = L"Inspect driver image";
  Status = gBS->HandleProtocol(Handle, &gEfiLoadedImageProtocolGuid, (VOID **)&Image);
  if (EFI_ERROR(Status))
    goto Done;
  if ((Image->ImageCodeType != EfiBootServicesCode) &&
      (Image->ImageCodeType != EfiRuntimeServicesCode))
  {
    Status = EFI_UNSUPPORTED;
    goto Done;
  }
  if ((Driver->Arguments != NULL) && (Driver->Arguments[0] != '\0'))
  {
    Stage = L"Convert driver arguments";
    Status = Utf8ToUnicode(Driver->Arguments, &Options, &OptionsSize);
    if (EFI_ERROR(Status))
      goto Done;
    Image->LoadOptions = Options;
    Image->LoadOptionsSize = (UINT32)OptionsSize;
  }
  Stage = L"Start driver";
  Status = gBS->StartImage(Handle, NULL, NULL);

Done:
  if (EFI_ERROR(Status))
  {
    InternalMainReport(Stage, Path == NULL ? Name : Path, Status);
    ReleaseFailedDriver(Handle, Options);
  }
  if (DevicePath != NULL)
    FreePool(DevicePath);
  if (Path != NULL)
    FreePool(Path);
  if (Name != NULL)
    FreePool(Name);
  return Status;
}

EFI_STATUS
AcLoadDrivers(IN EFI_HANDLE ImageHandle, IN CONST AC_GLOBAL_CONFIG *Config, IN BOOLEAN LoadEarly)
{
  CONST AC_UEFI_DRIVER_ENTRY *Driver;
  EFI_HANDLE                  Device;
  EFI_STATUS                  Status;
  UINTN                       Index;

  if ((ImageHandle == NULL) || (Config == NULL) ||
      ((Config->Uefi.DriverCount != 0) && (Config->Uefi.Drivers == NULL)))
    return EFI_INVALID_PARAMETER;
  Device = NULL;
  for (Index = 0; Index < Config->Uefi.DriverCount; ++Index)
  {
    Driver = &Config->Uefi.Drivers[Index];
    if (!Driver->Enabled || (Driver->LoadEarly != LoadEarly) || (Driver->Path == NULL) ||
        (Driver->Path[0] == '\0'))
      continue;
    if (Device == NULL)
    {
      Status = InternalMainSourceDevice(ImageHandle, &Device);
      if (EFI_ERROR(Status))
      {
        InternalMainReport(L"Locate driver source", NULL, Status);
        return Status;
      }
    }
    Status = LoadDriver(ImageHandle, Device, Driver);
    if (Status == EFI_OUT_OF_RESOURCES)
      return Status;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
AcConnectDrivers(VOID)
{
  EFI_HANDLE                          *Handles;
  EFI_GUID                           **Protocols;
  EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *Infos;
  BOOLEAN                             *Children;
  EFI_STATUS                           Status;
  UINTN                                Count;
  UINTN                                ProtocolCount;
  UINTN                                InfoCount;
  UINTN                                Index;
  UINTN                                ProtocolIndex;
  UINTN                                InfoIndex;
  UINTN                                ChildIndex;
  CONST CHAR16                        *Stage;

  Stage = L"Enumerate device handles";
  Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiDevicePathProtocolGuid, NULL, &Count, &Handles);
  if (Status == EFI_NOT_FOUND)
    return EFI_SUCCESS;
  if (EFI_ERROR(Status))
  {
    InternalMainReport(Stage, NULL, Status);
    return Status;
  }
  Children = AllocateZeroPool(Count * sizeof(*Children));
  if (Children == NULL)
  {
    FreePool(Handles);
    InternalMainReport(L"Allocate connection state", NULL, EFI_OUT_OF_RESOURCES);
    return EFI_OUT_OF_RESOURCES;
  }
  Protocols = NULL;
  Infos = NULL;
  for (Index = 0; Index < Count; ++Index)
  {
    Stage = L"Enumerate device protocols";
    Status = gBS->ProtocolsPerHandle(Handles[Index], &Protocols, &ProtocolCount);
    if (Status == EFI_OUT_OF_RESOURCES)
      goto Done;
    if (EFI_ERROR(Status))
    {
      InternalMainReport(Stage, NULL, Status);
      continue;
    }
    for (ProtocolIndex = 0; ProtocolIndex < ProtocolCount; ++ProtocolIndex)
    {
      Stage = L"Read device relationships";
      Status =
        gBS->OpenProtocolInformation(Handles[Index], Protocols[ProtocolIndex], &Infos, &InfoCount);
      if (Status == EFI_OUT_OF_RESOURCES)
        goto Done;
      if (EFI_ERROR(Status))
      {
        InternalMainReport(Stage, NULL, Status);
        continue;
      }
      for (InfoIndex = 0; InfoIndex < InfoCount; ++InfoIndex)
      {
        if ((Infos[InfoIndex].Attributes & EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER) == 0)
          continue;
        for (ChildIndex = 0; ChildIndex < Count; ++ChildIndex)
          if (Handles[ChildIndex] == Infos[InfoIndex].ControllerHandle)
            Children[ChildIndex] = TRUE;
      }
      if (Infos != NULL)
        FreePool(Infos);
      Infos = NULL;
    }
    FreePool(Protocols);
    Protocols = NULL;
  }
  for (Index = 0; Index < Count; ++Index)
  {
    if (Children[Index])
      continue;
    Stage = L"Connect device tree";
    Status = gBS->ConnectController(Handles[Index], NULL, NULL, TRUE);
    if (Status == EFI_OUT_OF_RESOURCES)
      goto Done;
    if (EFI_ERROR(Status) && (Status != EFI_NOT_FOUND) && (Status != EFI_UNSUPPORTED) &&
        (Status != EFI_ALREADY_STARTED))
      InternalMainReport(Stage, NULL, Status);
  }
  Status = EFI_SUCCESS;

Done:
  if (EFI_ERROR(Status))
    InternalMainReport(Stage, NULL, Status);
  if (Infos != NULL)
    FreePool(Infos);
  if (Protocols != NULL)
    FreePool(Protocols);
  FreePool(Children);
  FreePool(Handles);
  return Status;
}
