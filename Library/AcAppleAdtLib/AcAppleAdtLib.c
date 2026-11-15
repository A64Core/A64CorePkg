/** @file
  Apple Device Tree ownership and path-based mutation.
**/

#include "AdtInternal.h"

STATIC EFI_STATUS
ValidatePath(IN CONST CHAR8 *Path)
{
  CONST CHAR8 *Start;
  UINTN        Length;
  UINTN        Depth = 1;

  if (Path == NULL || *Path != '/')
    return EFI_INVALID_PARAMETER;
  ++Path;
  while (*Path != '\0')
  {
    Start = Path;
    Length = 0;
    while (*Path != '\0' && *Path != '/')
    {
      if ((UINT8)*Path < 0x21 || (UINT8)*Path > 0x7E || ++Length > 63)
        return EFI_INVALID_PARAMETER;
      ++Path;
    }
    if (Length == 0 || (Length == 1 && Start[0] == '.') ||
        (Length == 2 && Start[0] == '.' && Start[1] == '.'))
      return EFI_INVALID_PARAMETER;
    if (++Depth > AC_ADT_MAX_DEPTH)
      return EFI_BAD_BUFFER_SIZE;
    if (*Path == '/')
    {
      ++Path;
      if (*Path == '\0')
        return EFI_INVALID_PARAMETER;
    }
  }
  return EFI_SUCCESS;
}

STATIC BOOLEAN
ValidPropertyName(IN CONST CHAR8 *Name)
{
  UINTN Index;

  if (Name == NULL)
    return FALSE;
  for (Index = 0; Index < 32; ++Index)
  {
    if (Name[Index] == '\0')
      return Index != 0;
    if ((UINT8)Name[Index] < 0x21 || (UINT8)Name[Index] > 0x7E || Name[Index] == '/')
      return FALSE;
  }
  return FALSE;
}

STATIC CONST CHAR8 *
ReadComponent(IN CONST CHAR8 *Path, OUT CHAR8 Name[64])
{
  UINTN Length = 0;

  while (*Path != '\0' && *Path != '/')
    Name[Length++] = *Path++;
  Name[Length] = '\0';
  return *Path == '/' ? Path + 1 : Path;
}

STATIC AC_ADT_NODE *
FindChild(IN CONST AC_ADT_NODE *Parent, IN CONST CHAR8 *Name)
{
  AC_ADT_NODE *Child;

  for (Child = Parent->Children; Child != NULL; Child = Child->Next)
    if (AsciiStrCmp(Child->Name, Name) == 0)
      return Child;
  return NULL;
}

STATIC AC_ADT_NODE *
FindNode(IN CONST AC_APPLE_ADT_TREE *Tree, IN CONST CHAR8 *Path, OUT AC_ADT_NODE **Parent OPTIONAL)
{
  AC_ADT_NODE *Node = Tree->Root;
  CHAR8        Name[64];

  if (Parent != NULL)
    *Parent = NULL;
  ++Path;
  while (*Path != '\0')
  {
    Path = ReadComponent(Path, Name);
    if (Parent != NULL)
      *Parent = Node;
    Node = FindChild(Node, Name);
    if (Node == NULL)
      return NULL;
  }
  return Node;
}

STATIC UINTN
NodeSize(IN CONST AC_ADT_NODE *Node)
{
  return AC_ADT_NODE_HEADER_SIZE + AC_ADT_PROPERTY_HEADER_SIZE +
         ALIGN_VALUE(AsciiStrLen(Node->Name) + 1, 4);
}

STATIC UINTN
PropertySize(IN CONST AC_ADT_PROPERTY *Property)
{
  return AC_ADT_PROPERTY_HEADER_SIZE + ALIGN_VALUE(Property->Size, 4);
}

STATIC UINTN
FreeNode(IN AC_ADT_NODE *Node, IN OUT UINT32 *Count)
{
  AC_ADT_NODE     *Child;
  AC_ADT_NODE     *NextChild;
  AC_ADT_PROPERTY *Property;
  AC_ADT_PROPERTY *NextProperty;
  UINTN            Size = NodeSize(Node);

  for (Child = Node->Children; Child != NULL; Child = NextChild)
  {
    NextChild = Child->Next;
    Size += FreeNode(Child, Count);
  }
  for (Property = Node->Properties; Property != NULL; Property = NextProperty)
  {
    NextProperty = Property->Next;
    Size += PropertySize(Property);
    FreePool(Property);
  }
  ++*Count;
  FreePool(Node);
  return Size;
}

EFI_STATUS
AcAppleAdtCreate(OUT AC_APPLE_ADT_TREE **Tree)
{
  AC_APPLE_ADT_TREE *Result;

  if (Tree == NULL)
    return EFI_INVALID_PARAMETER;
  *Tree = NULL;
  Result = AllocateZeroPool(sizeof(*Result));
  if (Result == NULL)
    return EFI_OUT_OF_RESOURCES;
  Result->Root = AllocateZeroPool(sizeof(*Result->Root));
  if (Result->Root == NULL)
  {
    FreePool(Result);
    return EFI_OUT_OF_RESOURCES;
  }
  CopyMem(Result->Root->Name, "device-tree", sizeof("device-tree"));
  Result->NodeCount = 1;
  Result->SerializedSize = NodeSize(Result->Root);
  *Tree = Result;
  return EFI_SUCCESS;
}

VOID
AcAppleAdtFree(IN AC_APPLE_ADT_TREE *Tree OPTIONAL)
{
  UINT32 Count = 0;

  if (Tree == NULL)
    return;
  FreeNode(Tree->Root, &Count);
  FreePool(Tree);
}

EFI_STATUS
AcAppleAdtEnsureNode(IN OUT AC_APPLE_ADT_TREE *Tree, IN CONST CHAR8 *Path)
{
  AC_ADT_NODE  *Parent;
  AC_ADT_NODE  *Node;
  AC_ADT_NODE  *First = NULL;
  AC_ADT_NODE **Link;
  CHAR8         Name[64];
  UINTN         Size;
  UINTN         AddedSize = 0;
  UINT32        AddedCount = 0;
  UINT32        FreedCount = 0;
  EFI_STATUS    Status;

  if (Tree == NULL)
    return EFI_INVALID_PARAMETER;
  Status = ValidatePath(Path);
  if (EFI_ERROR(Status))
    return Status;
  Parent = Tree->Root;
  ++Path;
  while (*Path != '\0')
  {
    Path = ReadComponent(Path, Name);
    Node = FindChild(Parent, Name);
    if (Node != NULL)
    {
      Parent = Node;
      continue;
    }
    Link = &First;
    for (;;)
    {
      Size = AC_ADT_NODE_HEADER_SIZE + AC_ADT_PROPERTY_HEADER_SIZE +
             ALIGN_VALUE(AsciiStrLen(Name) + 1, 4);
      if (Tree->NodeCount + AddedCount >= AC_ADT_MAX_NODES ||
          Size > AC_ADT_MAX_SIZE - Tree->SerializedSize - AddedSize)
      {
        Status = EFI_BAD_BUFFER_SIZE;
        goto Failure;
      }
      Node = AllocateZeroPool(sizeof(*Node));
      if (Node == NULL)
      {
        Status = EFI_OUT_OF_RESOURCES;
        goto Failure;
      }
      CopyMem(Node->Name, Name, AsciiStrLen(Name) + 1);
      *Link = Node;
      ++AddedCount;
      AddedSize += Size;
      if (*Path == '\0')
        break;
      Link = &Node->Children;
      Path = ReadComponent(Path, Name);
    }
    Link = &Parent->Children;
    while (*Link != NULL)
      Link = &(*Link)->Next;
    *Link = First;
    Tree->NodeCount += AddedCount;
    Tree->SerializedSize += AddedSize;
    return EFI_SUCCESS;
  }
  return EFI_SUCCESS;

Failure:
  if (First != NULL)
    FreeNode(First, &FreedCount);
  return Status;
}

EFI_STATUS
AcAppleAdtGetChildName(IN CONST AC_APPLE_ADT_TREE *Tree, IN CONST CHAR8 *Path, IN UINTN Index,
                       OUT CONST CHAR8 **Name)
{
  AC_ADT_NODE *Node;
  EFI_STATUS   Status;

  if (Name != NULL)
    *Name = NULL;
  if (Tree == NULL || Name == NULL)
    return EFI_INVALID_PARAMETER;
  Status = ValidatePath(Path);
  if (EFI_ERROR(Status))
    return Status;
  Node = FindNode(Tree, Path, NULL);
  if (Node == NULL)
    return EFI_NOT_FOUND;
  Node = Node->Children;
  while (Node != NULL && Index != 0)
  {
    Node = Node->Next;
    --Index;
  }
  if (Node == NULL)
    return EFI_NOT_FOUND;
  *Name = Node->Name;
  return EFI_SUCCESS;
}

EFI_STATUS
AcAppleAdtDeleteNode(IN OUT AC_APPLE_ADT_TREE *Tree, IN CONST CHAR8 *Path)
{
  AC_ADT_NODE  *Parent;
  AC_ADT_NODE  *Node;
  AC_ADT_NODE **Link;
  UINT32        Count = 0;
  EFI_STATUS    Status;

  if (Tree == NULL)
    return EFI_INVALID_PARAMETER;
  Status = ValidatePath(Path);
  if (EFI_ERROR(Status))
    return Status;
  if (Path[1] == '\0')
    return EFI_ACCESS_DENIED;
  Node = FindNode(Tree, Path, &Parent);
  if (Node == NULL)
    return EFI_NOT_FOUND;
  Link = &Parent->Children;
  while (*Link != Node)
    Link = &(*Link)->Next;
  *Link = Node->Next;
  Tree->SerializedSize -= FreeNode(Node, &Count);
  Tree->NodeCount -= Count;
  return EFI_SUCCESS;
}

EFI_STATUS
AcAppleAdtGetProperty(IN CONST AC_APPLE_ADT_TREE *Tree, IN CONST CHAR8 *Path, IN CONST CHAR8 *Name,
                      OUT CONST VOID **Data, OUT UINTN *Size)
{
  AC_ADT_NODE     *Node;
  AC_ADT_PROPERTY *Property;
  EFI_STATUS       Status;

  if (Data != NULL)
    *Data = NULL;
  if (Size != NULL)
    *Size = 0;
  if (Tree == NULL || Data == NULL || Size == NULL || !ValidPropertyName(Name))
    return EFI_INVALID_PARAMETER;
  Status = ValidatePath(Path);
  if (EFI_ERROR(Status))
    return Status;
  Node = FindNode(Tree, Path, NULL);
  if (Node == NULL)
    return EFI_NOT_FOUND;
  if (AsciiStrCmp(Name, "name") == 0)
  {
    *Data = Node->Name;
    *Size = AsciiStrLen(Node->Name) + 1;
    return EFI_SUCCESS;
  }
  for (Property = Node->Properties; Property != NULL; Property = Property->Next)
    if (AsciiStrCmp(Property->Name, Name) == 0)
    {
      *Data = Property->Size == 0 ? NULL : Property->Data;
      *Size = Property->Size;
      return EFI_SUCCESS;
    }
  return EFI_NOT_FOUND;
}

EFI_STATUS
AcAppleAdtSetProperty(IN OUT AC_APPLE_ADT_TREE *Tree, IN CONST CHAR8 *Path, IN CONST CHAR8 *Name,
                      IN CONST VOID *Data OPTIONAL, IN UINTN Size)
{
  AC_ADT_NODE      *Node;
  AC_ADT_PROPERTY **Link;
  AC_ADT_PROPERTY  *Old;
  AC_ADT_PROPERTY  *Property;
  UINTN             NewSize;
  EFI_STATUS        Status;

  if (Tree == NULL || !ValidPropertyName(Name) || (Data == NULL && Size != 0))
    return EFI_INVALID_PARAMETER;
  Status = ValidatePath(Path);
  if (EFI_ERROR(Status))
    return Status;
  if (AsciiStrCmp(Name, "name") == 0)
    return EFI_ACCESS_DENIED;
  Node = FindNode(Tree, Path, NULL);
  if (Node == NULL)
    return EFI_NOT_FOUND;
  if (Size > AC_ADT_MAX_SIZE - AC_ADT_PROPERTY_HEADER_SIZE)
    return EFI_BAD_BUFFER_SIZE;
  Link = &Node->Properties;
  while (*Link != NULL && AsciiStrCmp((*Link)->Name, Name) != 0)
    Link = &(*Link)->Next;
  Old = *Link;
  NewSize = Tree->SerializedSize - (Old == NULL ? 0 : PropertySize(Old)) +
            AC_ADT_PROPERTY_HEADER_SIZE + ALIGN_VALUE(Size, 4);
  if (NewSize > AC_ADT_MAX_SIZE)
    return EFI_BAD_BUFFER_SIZE;
  Property = AllocateZeroPool(sizeof(*Property) + Size);
  if (Property == NULL)
    return EFI_OUT_OF_RESOURCES;
  CopyMem(Property->Name, Name, AsciiStrLen(Name) + 1);
  Property->Size = Size;
  if (Size != 0)
    CopyMem(Property->Data, Data, Size);
  Property->Next = Old == NULL ? NULL : Old->Next;
  *Link = Property;
  Tree->SerializedSize = NewSize;
  if (Old != NULL)
    FreePool(Old);
  return EFI_SUCCESS;
}

EFI_STATUS
AcAppleAdtDeleteProperty(IN OUT AC_APPLE_ADT_TREE *Tree, IN CONST CHAR8 *Path, IN CONST CHAR8 *Name)
{
  AC_ADT_NODE      *Node;
  AC_ADT_PROPERTY **Link;
  AC_ADT_PROPERTY  *Property;
  EFI_STATUS        Status;

  if (Tree == NULL || !ValidPropertyName(Name))
    return EFI_INVALID_PARAMETER;
  Status = ValidatePath(Path);
  if (EFI_ERROR(Status))
    return Status;
  if (AsciiStrCmp(Name, "name") == 0)
    return EFI_ACCESS_DENIED;
  Node = FindNode(Tree, Path, NULL);
  if (Node == NULL)
    return EFI_NOT_FOUND;
  Link = &Node->Properties;
  while (*Link != NULL && AsciiStrCmp((*Link)->Name, Name) != 0)
    Link = &(*Link)->Next;
  Property = *Link;
  if (Property == NULL)
    return EFI_NOT_FOUND;
  *Link = Property->Next;
  Tree->SerializedSize -= PropertySize(Property);
  FreePool(Property);
  return EFI_SUCCESS;
}
