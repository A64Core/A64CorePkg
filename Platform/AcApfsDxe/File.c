/** @file
  Synchronous read-only UEFI file and directory handles.
**/

#include "ApfsDxe.h"

STATIC EFI_STATUS EFIAPI FileOpen(EFI_FILE_PROTOCOL *This, EFI_FILE_PROTOCOL **NewHandle,
                                  CHAR16 *FileName, UINT64 OpenMode, UINT64 Attributes);
STATIC EFI_STATUS EFIAPI FileClose(EFI_FILE_PROTOCOL *This);
STATIC EFI_STATUS EFIAPI FileDelete(EFI_FILE_PROTOCOL *This);
STATIC EFI_STATUS EFIAPI FileRead(EFI_FILE_PROTOCOL *This, UINTN *BufferSize, VOID *Buffer);
STATIC EFI_STATUS EFIAPI FileWrite(EFI_FILE_PROTOCOL *This, UINTN *BufferSize, VOID *Buffer);
STATIC EFI_STATUS EFIAPI FileGetPosition(EFI_FILE_PROTOCOL *This, UINT64 *Position);
STATIC EFI_STATUS EFIAPI FileSetPosition(EFI_FILE_PROTOCOL *This, UINT64 Position);
STATIC EFI_STATUS EFIAPI FileSetInfo(EFI_FILE_PROTOCOL *This, EFI_GUID *InformationType,
                                     UINTN BufferSize, VOID *Buffer);
STATIC EFI_STATUS EFIAPI FileFlush(EFI_FILE_PROTOCOL *This);

STATIC CONST EFI_FILE_PROTOCOL mFileTemplate = {EFI_FILE_PROTOCOL_REVISION,
                                                FileOpen,
                                                FileClose,
                                                FileDelete,
                                                FileRead,
                                                FileWrite,
                                                FileGetPosition,
                                                FileSetPosition,
                                                ApfsDxeGetInfo,
                                                FileSetInfo,
                                                FileFlush,
                                                NULL,
                                                NULL,
                                                NULL,
                                                NULL};

STATIC EFI_STATUS
Canonicalize(CHAR8 *Path)
{
  UINTN Input = 0;
  UINTN Output = 1;
  UINTN Start;
  UINTN Length;

  while (Path[Input] != '\0')
  {
    while (Path[Input] == '/')
      ++Input;
    Start = Input;
    while (Path[Input] != '\0' && Path[Input] != '/')
      ++Input;
    Length = Input - Start;
    if (Length == 0 || (Length == 1 && Path[Start] == '.'))
      continue;
    if (Length == 2 && Path[Start] == '.' && Path[Start + 1] == '.')
    {
      if (Output == 1)
        return EFI_NOT_FOUND;
      while (Output > 1 && Path[Output - 1] != '/')
        --Output;
      if (Output > 1)
        --Output;
    }
    else
    {
      if (Output > 1)
        Path[Output++] = '/';
      CopyMem(Path + Output, Path + Start, Length);
      Output += Length;
    }
  }
  Path[Output] = '\0';
  return EFI_SUCCESS;
}

EFI_STATUS
ApfsDxeOpenFile(APFS_DXE_VOLUME *Volume, CHAR8 *Path, OUT EFI_FILE_PROTOCOL **Handle)
{
  APFS_DXE_FILE *File;
  EFI_STATUS     Status;

  *Handle = NULL;
  Status = ApfsDxeCheckMedia(Volume->Container);
  if (EFI_ERROR(Status))
    return Status;
  if (Volume->Apfs == NULL)
    return EFI_UNSUPPORTED;
  File = AllocateZeroPool(sizeof(*File));
  if (File == NULL)
    return EFI_OUT_OF_RESOURCES;
  Status = AcApfsOpenFile(Volume->Apfs, Path, &File->Apfs);
  if (EFI_ERROR(Status))
    goto Failure;
  Status = AcApfsGetFileInfo(File->Apfs, &File->Info);
  if (EFI_ERROR(Status))
    goto Failure;
  // Validate the original traversal before collapsing components; links remain unsupported.
  Status = Canonicalize(Path);
  if (EFI_ERROR(Status))
    goto Failure;
  File->Protocol = mFileTemplate;
  File->Volume = Volume;
  File->Path = Path;
  ++Volume->OpenFiles;
  *Handle = &File->Protocol;
  return EFI_SUCCESS;

Failure:
  AcApfsCloseFile(File->Apfs);
  FreePool(File);
  return ApfsDxeMapStatus(Status);
}

STATIC EFI_STATUS EFIAPI
FileOpen(EFI_FILE_PROTOCOL *This, EFI_FILE_PROTOCOL **NewHandle, CHAR16 *FileName, UINT64 OpenMode,
         UINT64 Attributes)
{
  APFS_DXE_FILE *Parent;
  CHAR8         *Name;
  CHAR8         *Path;
  UINTN          BaseSize;
  UINTN          NameSize;
  EFI_STATUS     Status;

  (VOID) Attributes;
  if (NewHandle == NULL)
    return EFI_INVALID_PARAMETER;
  *NewHandle = NULL;
  if (This == NULL || FileName == NULL)
    return EFI_INVALID_PARAMETER;
  if (OpenMode != EFI_FILE_MODE_READ)
    return (OpenMode & (EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE)) != 0 ? EFI_WRITE_PROTECTED
                                                                          : EFI_INVALID_PARAMETER;
  Parent = BASE_CR(This, APFS_DXE_FILE, Protocol);
  Status = ApfsDxeCheckMedia(Parent->Volume->Container);
  if (EFI_ERROR(Status))
    return Status;
  Status = ApfsDxeUnicodeToPath(FileName, &Name);
  if (EFI_ERROR(Status))
    return Status;
  if (Name[0] != '/' && Name[0] != '\0' && Parent->Info.Type != AcApfsFileDirectory)
  {
    FreePool(Name);
    return EFI_NOT_FOUND;
  }
  if (Name[0] == '/')
    Path = Name;
  else
  {
    BaseSize = AsciiStrLen(Parent->Path);
    NameSize = AsciiStrLen(Name);
    if (NameSize > MAX_UINTN - BaseSize - 2)
    {
      FreePool(Name);
      return EFI_OUT_OF_RESOURCES;
    }
    Path = AllocateZeroPool(BaseSize + NameSize + 2);
    if (Path == NULL)
    {
      FreePool(Name);
      return EFI_OUT_OF_RESOURCES;
    }
    CopyMem(Path, Parent->Path, BaseSize);
    if (NameSize != 0)
    {
      Path[BaseSize] = '/';
      CopyMem(Path + BaseSize + 1, Name, NameSize);
    }
    FreePool(Name);
  }
  Status = ApfsDxeOpenFile(Parent->Volume, Path, NewHandle);
  if (EFI_ERROR(Status))
    FreePool(Path);
  return Status;
}

STATIC EFI_STATUS EFIAPI
FileClose(EFI_FILE_PROTOCOL *This)
{
  APFS_DXE_FILE *File;

  if (This == NULL)
    return EFI_INVALID_PARAMETER;
  File = BASE_CR(This, APFS_DXE_FILE, Protocol);
  AcApfsCloseFile(File->Apfs);
  if (File->PendingEntry != NULL)
    FreePool(File->PendingEntry);
  FreePool(File->Path);
  --File->Volume->OpenFiles;
  FreePool(File);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
FileDelete(EFI_FILE_PROTOCOL *This)
{
  EFI_STATUS Status = FileClose(This);
  return EFI_ERROR(Status) ? Status : EFI_WARN_DELETE_FAILURE;
}

STATIC EFI_STATUS
ReadDirectory(APFS_DXE_FILE *File, UINTN *BufferSize, VOID *Buffer)
{
  AC_APFS_FILE_INFO Info;
  UINTN             Capacity = *BufferSize;
  UINTN             Required;
  EFI_STATUS        Status;

  *BufferSize = 0;
  if (File->PendingEntry == NULL)
  {
    Required = 0;
    Status = AcApfsReadDirectory(File->Apfs, &Required, NULL);
    if (Status == EFI_NOT_FOUND)
      return EFI_SUCCESS;
    if (Status != EFI_BUFFER_TOO_SMALL)
      return ApfsDxeMapStatus(Status);
    File->PendingEntry = AllocateZeroPool(Required);
    if (File->PendingEntry == NULL)
      return EFI_OUT_OF_RESOURCES;
    Status = AcApfsReadDirectory(File->Apfs, &Required, File->PendingEntry);
    if (EFI_ERROR(Status))
    {
      FreePool(File->PendingEntry);
      File->PendingEntry = NULL;
      return ApfsDxeMapStatus(Status);
    }
  }
  Status = AcApfsGetFileInfoById(File->Volume->Apfs, File->PendingEntry->FileId, &Info);
  if (EFI_ERROR(Status))
    return ApfsDxeMapStatus(Status == EFI_NOT_FOUND ? EFI_COMPROMISED_DATA : Status);
  if (File->PendingEntry->Type != AcApfsFileOther && Info.Type != File->PendingEntry->Type)
    return EFI_VOLUME_CORRUPTED;
  *BufferSize = Capacity;
  Status = ApfsDxeFillFileInfo(&Info, File->PendingEntry->Name, BufferSize, Buffer);
  if (!EFI_ERROR(Status))
  {
    FreePool(File->PendingEntry);
    File->PendingEntry = NULL;
  }
  return Status;
}

STATIC EFI_STATUS EFIAPI
FileRead(EFI_FILE_PROTOCOL *This, UINTN *BufferSize, VOID *Buffer)
{
  APFS_DXE_FILE *File;
  UINTN          Capacity;
  EFI_STATUS     Status;

  if (BufferSize == NULL)
    return EFI_INVALID_PARAMETER;
  Capacity = *BufferSize;
  *BufferSize = 0;
  if (This == NULL || (Buffer == NULL && Capacity != 0))
    return EFI_INVALID_PARAMETER;
  File = BASE_CR(This, APFS_DXE_FILE, Protocol);
  Status = ApfsDxeCheckMedia(File->Volume->Container);
  if (EFI_ERROR(Status))
    return Status;
  if (File->Info.Type == AcApfsFileDirectory)
  {
    *BufferSize = Capacity;
    return ReadDirectory(File, BufferSize, Buffer);
  }
  if (File->Position > File->Info.Size)
    return EFI_DEVICE_ERROR;
  *BufferSize = Capacity;
  Status = AcApfsReadFile(File->Apfs, File->Position, BufferSize, Buffer);
  File->Position += *BufferSize;
  return ApfsDxeMapStatus(Status);
}

STATIC EFI_STATUS EFIAPI
FileWrite(EFI_FILE_PROTOCOL *This, UINTN *BufferSize, VOID *Buffer)
{
  EFI_STATUS Status;
  (VOID) Buffer;
  if (BufferSize == NULL)
    return EFI_INVALID_PARAMETER;
  *BufferSize = 0;
  if (This == NULL)
    return EFI_INVALID_PARAMETER;
  Status = ApfsDxeCheckMedia(BASE_CR(This, APFS_DXE_FILE, Protocol)->Volume->Container);
  return EFI_ERROR(Status) ? Status : EFI_WRITE_PROTECTED;
}

STATIC EFI_STATUS EFIAPI
FileGetPosition(EFI_FILE_PROTOCOL *This, UINT64 *Position)
{
  APFS_DXE_FILE *File;
  EFI_STATUS     Status;
  if (Position == NULL)
    return EFI_INVALID_PARAMETER;
  *Position = 0;
  if (This == NULL)
    return EFI_INVALID_PARAMETER;
  File = BASE_CR(This, APFS_DXE_FILE, Protocol);
  Status = ApfsDxeCheckMedia(File->Volume->Container);
  if (EFI_ERROR(Status))
    return Status;
  if (File->Info.Type == AcApfsFileDirectory)
    return EFI_UNSUPPORTED;
  *Position = File->Position;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
FileSetPosition(EFI_FILE_PROTOCOL *This, UINT64 Position)
{
  APFS_DXE_FILE *File;
  EFI_STATUS     Status;
  if (This == NULL)
    return EFI_INVALID_PARAMETER;
  File = BASE_CR(This, APFS_DXE_FILE, Protocol);
  Status = ApfsDxeCheckMedia(File->Volume->Container);
  if (EFI_ERROR(Status))
    return Status;
  if (File->Info.Type == AcApfsFileDirectory)
  {
    if (Position != 0)
      return EFI_UNSUPPORTED;
    Status = AcApfsRewindDirectory(File->Apfs);
    if (!EFI_ERROR(Status) && File->PendingEntry != NULL)
    {
      FreePool(File->PendingEntry);
      File->PendingEntry = NULL;
    }
    return ApfsDxeMapStatus(Status);
  }
  File->Position = Position == MAX_UINT64 ? File->Info.Size : Position;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
FileSetInfo(EFI_FILE_PROTOCOL *This, EFI_GUID *InformationType, UINTN BufferSize, VOID *Buffer)
{
  EFI_STATUS Status;
  (VOID) BufferSize;
  (VOID) Buffer;
  if (This == NULL || InformationType == NULL)
    return EFI_INVALID_PARAMETER;
  Status = ApfsDxeCheckMedia(BASE_CR(This, APFS_DXE_FILE, Protocol)->Volume->Container);
  if (EFI_ERROR(Status))
    return Status;
  if (!CompareGuid(InformationType, &gEfiFileInfoGuid) &&
      !CompareGuid(InformationType, &gEfiFileSystemInfoGuid) &&
      !CompareGuid(InformationType, &gEfiFileSystemVolumeLabelInfoIdGuid))
    return EFI_UNSUPPORTED;
  return EFI_WRITE_PROTECTED;
}

STATIC EFI_STATUS EFIAPI
FileFlush(EFI_FILE_PROTOCOL *This)
{
  EFI_STATUS Status;
  if (This == NULL)
    return EFI_INVALID_PARAMETER;
  Status = ApfsDxeCheckMedia(BASE_CR(This, APFS_DXE_FILE, Protocol)->Volume->Container);
  return EFI_ERROR(Status) ? Status : EFI_ACCESS_DENIED;
}
