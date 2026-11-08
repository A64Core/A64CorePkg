/** @file
  File handles and bounded extent reads.
**/

#include "ApfsInternal.h"

EFI_STATUS
AcApfsOpenFile(IN AC_APFS_VOLUME *Volume, IN CONST CHAR8 *Path, OUT AC_APFS_FILE **File)
{
  APFS_INODE_INFO Inode;
  AC_APFS_FILE   *Result;
  EFI_STATUS      Status;

  if (File == NULL)
    return EFI_INVALID_PARAMETER;
  *File = NULL;
  if (Volume == NULL || Path == NULL)
    return EFI_INVALID_PARAMETER;
  Status = ApfsResolvePath(Volume, Path, &Inode);
  if (EFI_ERROR(Status))
    return Status;
  if ((Inode.Info.Type != AcApfsFileRegular && Inode.Info.Type != AcApfsFileDirectory) ||
      (Inode.BsdFlags & APFS_BSD_COMPRESSED) != 0 || Inode.CryptoId != 0)
    return EFI_UNSUPPORTED;
  Result = AllocateZeroPool(sizeof(*Result));
  if (Result == NULL)
    return EFI_OUT_OF_RESOURCES;
  Result->Volume = Volume;
  Result->Inode = Inode;
  *File = Result;
  return EFI_SUCCESS;
}

VOID
AcApfsCloseFile(IN AC_APFS_FILE *File OPTIONAL)
{
  if (File != NULL)
  {
    ApfsBtreeClose(File->DirectoryCursor);
    FreePool(File);
  }
}

EFI_STATUS
AcApfsGetFileInfo(IN CONST AC_APFS_FILE *File, OUT AC_APFS_FILE_INFO *Info)
{
  if (Info == NULL)
    return EFI_INVALID_PARAMETER;
  ZeroMem(Info, sizeof(*Info));
  if (File == NULL)
    return EFI_INVALID_PARAMETER;
  *Info = File->Inode.Info;
  return EFI_SUCCESS;
}

typedef struct
{
  UINT64 Start;
  UINT64 Length;
  UINT64 Physical;
} APFS_READ_EXTENT;

STATIC EFI_STATUS
ReadExtent(CONST AC_APFS_FILE *File, CONST APFS_RECORD *Record, UINT64 Header,
           OUT APFS_READ_EXTENT *Range)
{
  APFS_FILE_EXTENT Extent;
  UINT32           BlockSize;
  UINT64           Blocks;

  ZeroMem(Range, sizeof(*Range));
  if (ApfsRead64(Record->Key) != Header || Record->KeySize != 16 ||
      Record->ValueSize != sizeof(Extent))
    return EFI_COMPROMISED_DATA;
  Range->Start = ApfsRead64(Record->Key + 8);
  CopyMem(&Extent, Record->Value, sizeof(Extent));
  Range->Length = Extent.LengthAndFlags & APFS_EXTENT_LENGTH_MASK;
  Range->Physical = Extent.PhysicalBlock;
  BlockSize = File->Volume->Container->Superblock.BlockSize;
  if (Range->Length == 0 || Range->Start % BlockSize != 0 || Range->Length % BlockSize != 0 ||
      Range->Start > MAX_UINT64 - Range->Length)
    return EFI_COMPROMISED_DATA;
  if ((Extent.LengthAndFlags & ~APFS_EXTENT_LENGTH_MASK) != 0 || Extent.CryptoId != 0)
    return EFI_UNSUPPORTED;
  Blocks = Range->Length / BlockSize;
  if (Range->Physical != 0 &&
      (Range->Physical >= File->Volume->Container->Superblock.BlockCount ||
       Blocks > File->Volume->Container->Superblock.BlockCount - Range->Physical))
    return EFI_COMPROMISED_DATA;
  return EFI_SUCCESS;
}

EFI_STATUS
AcApfsReadFile(IN AC_APFS_FILE *File, IN UINT64 Offset, IN OUT UINTN *BufferSize,
               OUT VOID *Buffer OPTIONAL)
{
  APFS_CURSOR     *Cursor;
  APFS_CURSOR     *Previous;
  APFS_RECORD      Record;
  APFS_RECORD      Next;
  UINT64           Key[2];
  UINT64           Header;
  APFS_READ_EXTENT Extent;
  APFS_READ_EXTENT NextExtent;
  UINT64           Remaining;
  UINTN            Requested;
  UINTN            Chunk;
  UINTN            Done;
  EFI_STATUS       Status;
  EFI_STATUS       NextStatus;

  if (BufferSize == NULL)
    return EFI_INVALID_PARAMETER;
  Requested = *BufferSize;
  *BufferSize = 0;
  if (File == NULL || (Buffer == NULL && Requested != 0))
    return EFI_INVALID_PARAMETER;
  if (File->Inode.Info.Type != AcApfsFileRegular)
    return EFI_UNSUPPORTED;
  if (Requested == 0 || Offset >= File->Inode.Info.Size)
    return EFI_SUCCESS;
  Remaining = File->Inode.Info.Size - Offset;
  if (Remaining < Requested)
    Requested = (UINTN)Remaining;
  Header = File->Inode.StreamId | ((UINT64)APFS_RECORD_FILE_EXTENT << 60U);
  Key[0] = Header;
  Key[1] = Offset;
  Status = ApfsBtreeSeek(&File->Volume->Tree, Key, sizeof(Key), TRUE, &Cursor);
  if (EFI_ERROR(Status))
    return Status == EFI_NOT_FOUND ? EFI_COMPROMISED_DATA : Status;
  Done = 0;
  Status = ApfsBtreeRecord(Cursor, &Record);
  if (EFI_ERROR(Status))
    goto Exit;
  Status = ReadExtent(File, &Record, Header, &Extent);
  if (EFI_ERROR(Status))
    goto Exit;
  if (Extent.Start != 0)
  {
    Key[1] = Extent.Start - 1;
    Status = ApfsBtreeSeek(&File->Volume->Tree, Key, sizeof(Key), TRUE, &Previous);
    if (EFI_ERROR(Status))
    {
      if (Status == EFI_NOT_FOUND)
        Status = EFI_COMPROMISED_DATA;
      goto Exit;
    }
    Status = ApfsBtreeRecord(Previous, &Next);
    if (!EFI_ERROR(Status))
      Status = ReadExtent(File, &Next, Header, &NextExtent);
    ApfsBtreeClose(Previous);
    if (EFI_ERROR(Status))
      goto Exit;
    if (NextExtent.Start + NextExtent.Length != Extent.Start)
    {
      Status = EFI_COMPROMISED_DATA;
      goto Exit;
    }
  }
  for (;;)
  {
    if (Offset < Extent.Start || Offset - Extent.Start >= Extent.Length)
    {
      Status = EFI_COMPROMISED_DATA;
      break;
    }
    NextStatus = ApfsBtreeNext(Cursor);
    if (!EFI_ERROR(NextStatus))
    {
      NextStatus = ApfsBtreeRecord(Cursor, &Next);
      if (!EFI_ERROR(NextStatus) && ApfsRead64(Next.Key) == Header)
      {
        NextStatus = ReadExtent(File, &Next, Header, &NextExtent);
        if (!EFI_ERROR(NextStatus) && NextExtent.Start != Extent.Start + Extent.Length)
          NextStatus = EFI_COMPROMISED_DATA;
      }
      else if (!EFI_ERROR(NextStatus))
        NextStatus = EFI_NOT_FOUND;
    }
    if (EFI_ERROR(NextStatus) && NextStatus != EFI_NOT_FOUND)
    {
      Status = NextStatus;
      break;
    }
    if (NextStatus == EFI_NOT_FOUND && Extent.Start + Extent.Length < File->Inode.Info.Size)
    {
      Status = EFI_COMPROMISED_DATA;
      break;
    }
    Remaining = Extent.Length - (Offset - Extent.Start);
    Chunk = Requested - Done;
    if (Remaining < Chunk)
      Chunk = (UINTN)Remaining;
    if (Extent.Physical == 0)
      ZeroMem((UINT8 *)Buffer + Done, Chunk);
    else
    {
      Status = ApfsReadBytes(File->Volume->Container,
                             Extent.Physical * File->Volume->Container->Superblock.BlockSize +
                               Offset - Extent.Start,
                             Chunk, (UINT8 *)Buffer + Done);
      if (EFI_ERROR(Status))
        break;
    }
    Done += Chunk;
    Offset += Chunk;
    if (Done == Requested)
    {
      Status = EFI_SUCCESS;
      break;
    }
    if (NextStatus == EFI_NOT_FOUND)
    {
      Status = EFI_COMPROMISED_DATA;
      break;
    }
    Extent = NextExtent;
  }

Exit:
  *BufferSize = Done;
  ApfsBtreeClose(Cursor);
  return Status;
}
