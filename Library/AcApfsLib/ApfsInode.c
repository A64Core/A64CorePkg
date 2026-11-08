/** @file
  Inode and extended-field decoding.
**/

#include "ApfsInternal.h"

EFI_STATUS
ApfsValidateXfields(CONST UINT8 *Buffer, UINTN Size, OUT APFS_DSTREAM *Stream OPTIONAL,
                    OUT BOOLEAN *HasStream OPTIONAL)
{
  UINTN   Count;
  UINTN   Start;
  UINTN   Offset;
  UINTN   Length;
  UINTN   Padded;
  UINTN   Index;
  UINT8   Type;
  BOOLEAN Seen[256];

  if (HasStream != NULL)
    *HasStream = FALSE;
  if (Stream != NULL)
    ZeroMem(Stream, sizeof(*Stream));
  if (Size == 0)
    return EFI_SUCCESS;
  if (Size < sizeof(APFS_XFIELD_BLOB))
    return EFI_COMPROMISED_DATA;
  Count = ApfsRead16(Buffer);
  if (Count > (Size - sizeof(APFS_XFIELD_BLOB)) / sizeof(APFS_XFIELD))
    return EFI_COMPROMISED_DATA;
  Start = sizeof(APFS_XFIELD_BLOB) + Count * sizeof(APFS_XFIELD);
  // xf_used_data counts padded values, excluding their descriptors.
  if (ApfsRead16(Buffer + 2) != Size - Start)
    return EFI_COMPROMISED_DATA;
  ZeroMem(Seen, sizeof(Seen));
  Offset = Start;
  for (Index = 0; Index < Count; ++Index)
  {
    Type = Buffer[sizeof(APFS_XFIELD_BLOB) + Index * sizeof(APFS_XFIELD)];
    Length = ApfsRead16(Buffer + sizeof(APFS_XFIELD_BLOB) + Index * sizeof(APFS_XFIELD) + 2);
    Padded = (Length + 7U) & ~(UINTN)7U;
    if (Seen[Type] || Padded > Size - Offset)
      return EFI_COMPROMISED_DATA;
    Seen[Type] = TRUE;
    if (Stream != NULL && Type == APFS_INODE_EXT_DSTREAM)
    {
      if (Length != sizeof(*Stream))
        return EFI_COMPROMISED_DATA;
      CopyMem(Stream, Buffer + Offset, sizeof(*Stream));
      if (HasStream != NULL)
        *HasStream = TRUE;
    }
    if (Stream != NULL && (Type == 1 || Type == 2 || Type == 5))
      return EFI_UNSUPPORTED;
    Offset += Padded;
  }
  return Offset == Size ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
}

EFI_STATUS
ApfsReadInode(AC_APFS_VOLUME *Volume, UINT64 Id, OUT APFS_INODE_INFO *Info)
{
  APFS_CURSOR *Cursor;
  APFS_RECORD  Record;
  APFS_INODE   Inode;
  APFS_DSTREAM Stream;
  UINT64       Key;
  EFI_STATUS   Status;
  BOOLEAN      HasStream;

  ZeroMem(Info, sizeof(*Info));
  if (Id == 0 || Id > APFS_ID_MASK)
    return EFI_COMPROMISED_DATA;
  Key = Id | ((UINT64)APFS_RECORD_INODE << 60U);
  Status = ApfsBtreeSeek(&Volume->Tree, &Key, sizeof(Key), FALSE, &Cursor);
  if (EFI_ERROR(Status))
    return Status;
  Status = ApfsBtreeRecord(Cursor, &Record);
  if (EFI_ERROR(Status))
    goto Done;
  if (ApfsRead64(Record.Key) != Key)
  {
    Status = EFI_NOT_FOUND;
    goto Done;
  }
  Status = EFI_COMPROMISED_DATA;
  if (Record.ValueSize < sizeof(Inode))
    goto Done;
  CopyMem(&Inode, Record.Value, sizeof(Inode));
  Status = ApfsValidateXfields(Record.Value + sizeof(Inode), Record.ValueSize - sizeof(Inode),
                               &Stream, &HasStream);
  if (EFI_ERROR(Status))
    goto Done;
  if ((Inode.InternalFlags & APFS_INODE_BEING_TRUNCATED) != 0)
  {
    Status = EFI_UNSUPPORTED;
    goto Done;
  }
  Info->Info.FileId = Id;
  Info->Info.CreateTime = Inode.CreateTime;
  Info->Info.AccessTime = Inode.AccessTime;
  Info->Info.ModificationTime = Inode.ModificationTime;
  Info->Info.BsdFlags = Inode.BsdFlags;
  switch (Inode.Mode & APFS_MODE_TYPE_MASK)
  {
  case APFS_MODE_REGULAR:
    Info->Info.Type = AcApfsFileRegular;
    break;
  case APFS_MODE_DIRECTORY:
    Info->Info.Type = AcApfsFileDirectory;
    break;
  case APFS_MODE_SYMLINK:
    Info->Info.Type = AcApfsFileSymlink;
    break;
  default:
    Info->Info.Type = AcApfsFileOther;
    break;
  }
  Info->ParentId = Inode.ParentId;
  Info->StreamId = Inode.PrivateId;
  Info->InternalFlags = Inode.InternalFlags;
  Info->BsdFlags = Inode.BsdFlags;
  if (HasStream)
  {
    if (Inode.PrivateId == 0 || Inode.PrivateId > APFS_ID_MASK)
    {
      Status = EFI_COMPROMISED_DATA;
      goto Done;
    }
    Info->Info.Size = Stream.Size;
    Info->Info.AllocatedSize = Stream.AllocatedSize;
    Info->CryptoId = Stream.DefaultCryptoId;
  }
  Status = EFI_SUCCESS;

Done:
  ApfsBtreeClose(Cursor);
  if (EFI_ERROR(Status))
    ZeroMem(Info, sizeof(*Info));
  return Status;
}

EFI_STATUS
AcApfsGetFileInfoById(IN AC_APFS_VOLUME *Volume, IN UINT64 FileId, OUT AC_APFS_FILE_INFO *Info)
{
  APFS_INODE_INFO Inode;
  EFI_STATUS      Status;

  if (Info == NULL)
    return EFI_INVALID_PARAMETER;
  ZeroMem(Info, sizeof(*Info));
  if (Volume == NULL || FileId == 0 || FileId > APFS_ID_MASK)
    return EFI_INVALID_PARAMETER;
  Status = ApfsReadInode(Volume, FileId, &Inode);
  if (EFI_ERROR(Status))
    return Status;
  if ((Inode.Info.Type != AcApfsFileRegular && Inode.Info.Type != AcApfsFileDirectory) ||
      (Inode.BsdFlags & APFS_BSD_COMPRESSED) != 0 || Inode.CryptoId != 0)
    return EFI_UNSUPPORTED;
  *Info = Inode.Info;
  return EFI_SUCCESS;
}
