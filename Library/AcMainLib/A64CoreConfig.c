/** @file
  Configuration access on the application's source partition.
**/

#include "MainInternal.h"

#include <Guid/FileInfo.h>
#include <Protocol/SimpleFileSystem.h>

#define AC_CONFIG_PATH L"\\EFI\\AC\\config.plist"
#define AC_CONFIG_MAX_SIZE (16U * 1024U * 1024U)
#define AC_FILE_INFO_MAX_SIZE (64U * 1024U)

VOID
InternalMainReport(IN CONST CHAR16 *Stage, IN CONST CHAR16 *Path OPTIONAL, IN EFI_STATUS Status)
{
  if ((gST != NULL) && (gST->ConOut != NULL))
    Print(L"A64Core: %s%s%s: %r\r\n", Stage, Path == NULL ? L"" : L" ", Path == NULL ? L"" : Path,
          Status);
  else
    DEBUG((DEBUG_ERROR, "AC: %s %s: %r\n", Stage, Path == NULL ? L"" : Path, Status));
}

EFI_STATUS
InternalMainSourceDevice(IN EFI_HANDLE ImageHandle, OUT EFI_HANDLE *Device)
{
  EFI_LOADED_IMAGE_PROTOCOL *Image;
  EFI_STATUS                 Status;

  *Device = NULL;
  if (ImageHandle == NULL)
    return EFI_INVALID_PARAMETER;
  Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Image);
  if (EFI_ERROR(Status))
    return Status;
  if ((Image->DeviceHandle == NULL) || (DevicePathFromHandle(Image->DeviceHandle) == NULL))
    return EFI_NOT_FOUND;
  *Device = Image->DeviceHandle;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ReadConfiguration(EFI_FILE_PROTOCOL *File, VOID **Buffer, UINTN *BufferSize)
{
  EFI_FILE_INFO *Info;
  EFI_STATUS     Status;
  UINT8         *Data;
  UINTN          InfoSize;
  UINTN          Capacity;
  UINTN          Size;
  UINTN          Offset;
  UINTN          Read;

  *Buffer = NULL;
  *BufferSize = 0;
  InfoSize = 0;
  Status = File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL)
    return EFI_ERROR(Status) ? Status : EFI_DEVICE_ERROR;
  if ((InfoSize < SIZE_OF_EFI_FILE_INFO + sizeof(CHAR16)) || (InfoSize > AC_FILE_INFO_MAX_SIZE))
    return EFI_VOLUME_CORRUPTED;
  Info = AllocatePool(InfoSize);
  if (Info == NULL)
    return EFI_OUT_OF_RESOURCES;
  Capacity = InfoSize;
  Status = File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, Info);
  if (!EFI_ERROR(Status))
  {
    if ((InfoSize > Capacity) || (InfoSize < SIZE_OF_EFI_FILE_INFO + sizeof(CHAR16)) ||
        (Info->Size < SIZE_OF_EFI_FILE_INFO + sizeof(CHAR16)) || (Info->Size > InfoSize) ||
        ((Info->Attribute & EFI_FILE_DIRECTORY) != 0))
      Status = EFI_VOLUME_CORRUPTED;
    else if (Info->FileSize > AC_CONFIG_MAX_SIZE)
      Status = EFI_BAD_BUFFER_SIZE;
  }
  Size = EFI_ERROR(Status) ? 0 : (UINTN)Info->FileSize;
  FreePool(Info);
  if (EFI_ERROR(Status))
    return Status;
  Data = AllocatePool(Size == 0 ? 1 : Size);
  if (Data == NULL)
    return EFI_OUT_OF_RESOURCES;
  for (Offset = 0; Offset < Size; Offset += Read)
  {
    Read = Size - Offset;
    Status = File->Read(File, &Read, Data + Offset);
    if (!EFI_ERROR(Status) && ((Read == 0) || (Read > Size - Offset)))
      Status = EFI_DEVICE_ERROR;
    if (EFI_ERROR(Status))
    {
      FreePool(Data);
      return Status;
    }
  }
  *Buffer = Data;
  *BufferSize = Size;
  return EFI_SUCCESS;
}

EFI_STATUS
AcLoadConfiguration(IN EFI_HANDLE ImageHandle, OUT AC_GLOBAL_CONFIG *Config)
{
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
  EFI_FILE_PROTOCOL               *Root;
  EFI_FILE_PROTOCOL               *File;
  EFI_HANDLE                       Device;
  EFI_STATUS                       Status;
  CONST CHAR16                    *Stage;
  VOID                            *Buffer;
  UINTN                            BufferSize;
  UINTN                            Issues;

  if (Config == NULL)
    return EFI_INVALID_PARAMETER;
  ZeroMem(Config, sizeof(*Config));
  Stage = L"Locate configuration source";
  Status = InternalMainSourceDevice(ImageHandle, &Device);
  if (EFI_ERROR(Status))
    goto Done;
  Status = gBS->HandleProtocol(Device, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&FileSystem);
  if (EFI_ERROR(Status))
    goto Done;
  Stage = L"Open source file system";
  Status = FileSystem->OpenVolume(FileSystem, &Root);
  if (EFI_ERROR(Status))
    goto Done;
  Stage = L"Open configuration";
  Status = Root->Open(Root, &File, AC_CONFIG_PATH, EFI_FILE_MODE_READ, 0);
  Root->Close(Root);
  if (EFI_ERROR(Status))
    goto Done;
  Stage = L"Read configuration";
  Status = ReadConfiguration(File, &Buffer, &BufferSize);
  File->Close(File);
  if (EFI_ERROR(Status))
    goto Done;
  Stage = L"Parse configuration";
  Status = AcConfigurationInit(Buffer, BufferSize, Config, &Issues);
  FreePool(Buffer);
  if ((Issues != 0) && (gST != NULL) && (gST->ConOut != NULL))
    Print(L"A64Core: configuration contains %u issue(s).\r\n", (UINT32)Issues);

Done:
  if (EFI_ERROR(Status))
    InternalMainReport(Stage, AC_CONFIG_PATH, Status);
  return Status;
}
