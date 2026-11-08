/** @file
  Directory record decoding, path lookup, and directory enumeration.
**/

#include "ApfsInternal.h"

STATIC AC_APFS_FILE_TYPE
DirectoryType(UINT16 Flags)
{
  switch (Flags & 0xFU)
  {
  case 4:
    return AcApfsFileDirectory;
  case 8:
    return AcApfsFileRegular;
  case 10:
    return AcApfsFileSymlink;
  default:
    return AcApfsFileOther;
  }
}

EFI_STATUS
ApfsParseDirectoryEntry(AC_APFS_VOLUME *Volume, CONST APFS_RECORD *Record, OUT CONST CHAR8 **Name,
                        OUT UINTN *Length, OUT APFS_DIRECTORY_VALUE *Value,
                        OUT APFS_NAME *Normalized)
{
  UINT32     Hash;
  EFI_STATUS Status;

  ZeroMem(Normalized, sizeof(*Normalized));
  Status = ApfsDirectoryKey(Record, Volume->Tree.HashedNames, Name, Length, &Hash);
  if (EFI_ERROR(Status))
    return Status;
  if (Record->ValueSize < sizeof(*Value))
    return EFI_COMPROMISED_DATA;
  CopyMem(Value, Record->Value, sizeof(*Value));
  if (Value->FileId == 0 || Value->FileId > APFS_ID_MASK || (*Length == 1 && (*Name)[0] == '.') ||
      (*Length == 2 && (*Name)[0] == '.' && (*Name)[1] == '.'))
    return EFI_COMPROMISED_DATA;
  if ((Value->Flags & ~0xFU) != 0)
    return EFI_UNSUPPORTED;
  Status = ApfsValidateXfields(Record->Value + sizeof(*Value), Record->ValueSize - sizeof(*Value),
                               NULL, NULL);
  if (EFI_ERROR(Status))
    return Status;
  Status = ApfsNormalizeName(*Name, *Length, Volume->Tree.HashedNames,
                             (Volume->IncompatibleFeatures & APFS_INCOMPAT_CASE_INSENSITIVE) != 0,
                             Normalized);
  if (EFI_ERROR(Status))
    return Status;
  if (Volume->Tree.HashedNames && Normalized->Hash != Hash)
  {
    Status = EFI_COMPROMISED_DATA;
    ApfsFreeName(Normalized);
  }
  return Status;
}

STATIC EFI_STATUS
FindChild(AC_APFS_VOLUME *Volume, UINT64 Parent, CONST CHAR8 *Name, UINTN Length,
          APFS_DIRECTORY_VALUE *Value)
{
  APFS_NAME    Search;
  APFS_NAME    Candidate;
  APFS_CURSOR *Cursor;
  APFS_RECORD  Record;
  EFI_STATUS   Status;
  UINT8       *Key;
  UINT64       Header;
  UINT32       Packed;
  UINTN        KeySize;
  UINTN        CandidateLength;
  CONST CHAR8 *CandidateName;

  Status = ApfsNormalizeName(Name, Length, Volume->Tree.HashedNames,
                             (Volume->IncompatibleFeatures & APFS_INCOMPAT_CASE_INSENSITIVE) != 0,
                             &Search);
  if (EFI_ERROR(Status))
    return Status == EFI_COMPROMISED_DATA ? EFI_INVALID_PARAMETER : Status;
  if (Volume->Tree.HashedNames && Length >= APFS_NAME_LENGTH_MASK)
  {
    ApfsFreeName(&Search);
    return EFI_INVALID_PARAMETER;
  }
  Header = Parent | ((UINT64)APFS_RECORD_DIRECTORY << 60U);
  KeySize = Volume->Tree.HashedNames ? 13 : 10 + Length + 1;
  Key = AllocateZeroPool(KeySize);
  if (Key == NULL)
  {
    ApfsFreeName(&Search);
    return EFI_OUT_OF_RESOURCES;
  }
  CopyMem(Key, &Header, sizeof(Header));
  if (Volume->Tree.HashedNames)
  {
    Packed = (Search.Hash << 10U) | 1U;
    CopyMem(Key + 8, &Packed, sizeof(Packed));
  }
  else
  {
    UINT16 NameSize;
    NameSize = (UINT16)(Length + 1);
    CopyMem(Key + 8, &NameSize, sizeof(NameSize));
    CopyMem(Key + 10, Name, Length);
  }
  Status = ApfsBtreeSeek(&Volume->Tree, Key, KeySize, FALSE, &Cursor);
  FreePool(Key);
  if (EFI_ERROR(Status))
  {
    ApfsFreeName(&Search);
    return Status;
  }
  for (;;)
  {
    Status = ApfsBtreeRecord(Cursor, &Record);
    if (EFI_ERROR(Status))
      break;
    if (ApfsRead64(Record.Key) != Header ||
        (Volume->Tree.HashedNames && (ApfsRead32(Record.Key + 8) >> 10U) != Search.Hash))
    {
      Status = EFI_NOT_FOUND;
      break;
    }
    Status =
      ApfsParseDirectoryEntry(Volume, &Record, &CandidateName, &CandidateLength, Value, &Candidate);
    if (EFI_ERROR(Status))
      break;
    if (Candidate.Count == Search.Count &&
        CompareMem(Candidate.Points, Search.Points, Search.Count * sizeof(UINT32)) == 0)
    {
      ApfsFreeName(&Candidate);
      Status = EFI_SUCCESS;
      break;
    }
    ApfsFreeName(&Candidate);
    if (!Volume->Tree.HashedNames)
    {
      Status = EFI_NOT_FOUND;
      break;
    }
    Status = ApfsBtreeNext(Cursor);
    if (EFI_ERROR(Status))
      break;
  }
  ApfsBtreeClose(Cursor);
  ApfsFreeName(&Search);
  return Status;
}

EFI_STATUS
ApfsResolvePath(AC_APFS_VOLUME *Volume, CONST CHAR8 *Path, OUT APFS_INODE_INFO *Info)
{
  APFS_INODE_INFO      Current;
  APFS_DIRECTORY_VALUE Child;
  EFI_STATUS           Status;
  UINTN                Offset;
  UINTN                Start;
  UINTN                Length;

  if (Path[0] != '/')
    return EFI_INVALID_PARAMETER;
  Status = ApfsReadInode(Volume, Volume->RootId, &Current);
  if (EFI_ERROR(Status))
    return Status == EFI_NOT_FOUND ? EFI_COMPROMISED_DATA : Status;
  Offset = 0;
  while (Path[Offset] != '\0')
  {
    if (Current.Info.Type != AcApfsFileDirectory)
      return EFI_NOT_FOUND;
    while (Path[Offset] == '/')
      ++Offset;
    if (Path[Offset] == '\0')
      break;
    Start = Offset;
    while (Path[Offset] != '\0' && Path[Offset] != '/')
      ++Offset;
    Length = Offset - Start;
    if (Length == 1 && Path[Start] == '.')
      continue;
    if (Length == 2 && Path[Start] == '.' && Path[Start + 1] == '.')
    {
      if (Current.Info.FileId != Volume->RootId)
      {
        Status = ApfsReadInode(Volume, Current.ParentId, &Current);
        if (EFI_ERROR(Status))
          return Status == EFI_NOT_FOUND ? EFI_COMPROMISED_DATA : Status;
        if (Current.Info.Type != AcApfsFileDirectory)
          return EFI_COMPROMISED_DATA;
      }
      continue;
    }
    Status = FindChild(Volume, Current.Info.FileId, Path + Start, Length, &Child);
    if (EFI_ERROR(Status))
      return Status;
    Status = ApfsReadInode(Volume, Child.FileId, &Current);
    if (EFI_ERROR(Status))
      return Status == EFI_NOT_FOUND ? EFI_COMPROMISED_DATA : Status;
    if ((Child.Flags & 0xFU) != 0 && Current.Info.Type != DirectoryType(Child.Flags))
      return EFI_COMPROMISED_DATA;
    if (Current.Info.Type == AcApfsFileSymlink || Current.Info.Type == AcApfsFileOther ||
        (Current.BsdFlags & APFS_BSD_COMPRESSED) != 0 || Current.CryptoId != 0)
      return EFI_UNSUPPORTED;
  }
  *Info = Current;
  return EFI_SUCCESS;
}

EFI_STATUS
AcApfsReadDirectory(IN AC_APFS_FILE *Directory, IN OUT UINTN *BufferSize,
                    OUT AC_APFS_DIRECTORY_ENTRY *Entry OPTIONAL)
{
  APFS_RECORD             Record;
  APFS_DIRECTORY_VALUE    Value;
  EFI_STATUS              Status;
  CONST CHAR8            *Name;
  UINTN                   Length;
  UINTN                   Capacity;
  UINTN                   Required;
  UINT8                   Key[13];
  UINT64                  Header;
  AC_APFS_DIRECTORY_ENTRY Prefix;
  APFS_NAME               Normalized;

  if (BufferSize == NULL)
    return EFI_INVALID_PARAMETER;
  Capacity = *BufferSize;
  *BufferSize = 0;
  if (Directory == NULL)
    return EFI_INVALID_PARAMETER;
  if (Directory->Inode.Info.Type != AcApfsFileDirectory)
    return EFI_UNSUPPORTED;
  if (Directory->DirectoryEnd)
    return EFI_NOT_FOUND;
  Header = Directory->Inode.Info.FileId | ((UINT64)APFS_RECORD_DIRECTORY << 60U);
  if (Directory->DirectoryCursor == NULL)
  {
    ZeroMem(Key, sizeof(Key));
    CopyMem(Key, &Header, sizeof(Header));
    Key[8] = 1;
    Status =
      ApfsBtreeSeek(&Directory->Volume->Tree, Key, Directory->Volume->Tree.HashedNames ? 13 : 11,
                    FALSE, &Directory->DirectoryCursor);
  }
  else if (Directory->AdvanceDirectory)
  {
    Status = ApfsBtreeNext(Directory->DirectoryCursor);
    if (!EFI_ERROR(Status))
      Directory->AdvanceDirectory = FALSE;
  }
  else
    Status = EFI_SUCCESS;
  if (Status == EFI_NOT_FOUND)
    Directory->DirectoryEnd = TRUE;
  if (EFI_ERROR(Status))
    return Status;
  Status = ApfsBtreeRecord(Directory->DirectoryCursor, &Record);
  if (EFI_ERROR(Status))
    return Status;
  if (ApfsRead64(Record.Key) != Header)
  {
    Directory->DirectoryEnd = TRUE;
    return EFI_NOT_FOUND;
  }
  Status = ApfsParseDirectoryEntry(Directory->Volume, &Record, &Name, &Length, &Value, &Normalized);
  if (EFI_ERROR(Status))
    return Status;
  ApfsFreeName(&Normalized);
  Required = OFFSET_OF(AC_APFS_DIRECTORY_ENTRY, Name) + Length + 1;
  *BufferSize = Required;
  if (Entry == NULL || Capacity < Required)
    return EFI_BUFFER_TOO_SMALL;
  ZeroMem(&Prefix, sizeof(Prefix));
  Prefix.FileId = Value.FileId;
  Prefix.Type = DirectoryType(Value.Flags);
  CopyMem(Entry, &Prefix, OFFSET_OF(AC_APFS_DIRECTORY_ENTRY, Name));
  CopyMem((UINT8 *)Entry + OFFSET_OF(AC_APFS_DIRECTORY_ENTRY, Name), Name, Length + 1);
  Directory->AdvanceDirectory = TRUE;
  return EFI_SUCCESS;
}

EFI_STATUS
AcApfsRewindDirectory(IN AC_APFS_FILE *Directory)
{
  if (Directory == NULL)
    return EFI_INVALID_PARAMETER;
  if (Directory->Inode.Info.Type != AcApfsFileDirectory)
    return EFI_UNSUPPORTED;
  ApfsBtreeClose(Directory->DirectoryCursor);
  Directory->DirectoryCursor = NULL;
  Directory->AdvanceDirectory = FALSE;
  Directory->DirectoryEnd = FALSE;
  return EFI_SUCCESS;
}
