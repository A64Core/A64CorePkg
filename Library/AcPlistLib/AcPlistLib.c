/** @file
  Property-list document ownership and node access.
**/

#include "PlistInternal.h"

EFI_STATUS
AcPlistParse(IN CONST VOID *Buffer, IN UINTN BufferSize, OUT AC_PLIST_DOCUMENT **Document)
{
  EFI_STATUS         Status;
  AC_PLIST_DOCUMENT *Result;

  if (Document == NULL)
    return EFI_INVALID_PARAMETER;
  *Document = NULL;
  if (Buffer == NULL)
    return EFI_INVALID_PARAMETER;
  if (BufferSize > AC_PLIST_MAX_SIZE)
    return EFI_BAD_BUFFER_SIZE;

  Result = AllocateZeroPool(sizeof(*Result));
  if (Result == NULL)
    return EFI_OUT_OF_RESOURCES;
  Status = InternalPlistParse(Buffer, BufferSize, Result);
  if (EFI_ERROR(Status))
  {
    AcPlistFree(Result);
    return Status;
  }
  *Document = Result;
  return EFI_SUCCESS;
}

VOID
AcPlistFree(IN AC_PLIST_DOCUMENT *Document OPTIONAL)
{
  AC_PLIST_NODE *Node;
  AC_PLIST_NODE *Next;

  if (Document == NULL)
    return;
  for (Node = Document->Nodes; Node != NULL; Node = Next)
  {
    Next = Node->NextAllocated;
    if (Node->Text != NULL)
      FreePool(Node->Text);
    if (Node->Key != NULL)
      FreePool(Node->Key);
    if (Node->Children != NULL)
      FreePool(Node->Children);
    FreePool(Node);
  }
  FreePool(Document);
}

CONST AC_PLIST_NODE *
AcPlistGetRoot(IN CONST AC_PLIST_DOCUMENT *Document)
{
  return Document == NULL ? NULL : Document->Root;
}

AC_PLIST_TYPE
AcPlistGetType(IN CONST AC_PLIST_NODE *Node OPTIONAL)
{
  return Node == NULL ? AcPlistTypeUnknown : Node->Type;
}

UINTN
AcPlistGetCount(IN CONST AC_PLIST_NODE *Node OPTIONAL)
{
  if ((Node == NULL) || ((Node->Type != AcPlistTypeArray) && (Node->Type != AcPlistTypeDictionary)))
    return 0;
  return Node->Count;
}

CONST AC_PLIST_NODE *
AcPlistGetArrayItem(IN CONST AC_PLIST_NODE *Node, IN UINTN Index)
{
  if ((Node == NULL) || (Node->Type != AcPlistTypeArray) || (Index >= Node->Count))
    return NULL;
  return Node->Children[Index];
}

EFI_STATUS
AcPlistGetDictionaryItem(IN CONST AC_PLIST_NODE *Node, IN UINTN Index, OUT CONST CHAR8 **Key,
                         OUT CONST AC_PLIST_NODE **Value)
{
  if (Key != NULL)
    *Key = NULL;
  if (Value != NULL)
    *Value = NULL;
  if ((Node == NULL) || (Key == NULL) || (Value == NULL) || (Node->Type != AcPlistTypeDictionary))
    return EFI_INVALID_PARAMETER;
  if (Index >= Node->Count)
    return EFI_NOT_FOUND;
  *Key = Node->Children[Index]->Key;
  *Value = Node->Children[Index];
  return EFI_SUCCESS;
}

CONST CHAR8 *
AcPlistGetString(IN CONST AC_PLIST_NODE *Node)
{
  return ((Node == NULL) || (Node->Type != AcPlistTypeString)) ? NULL : Node->Text;
}

EFI_STATUS
AcPlistGetBoolean(IN CONST AC_PLIST_NODE *Node, OUT BOOLEAN *Value)
{
  if (Value == NULL)
    return EFI_INVALID_PARAMETER;
  *Value = FALSE;
  if ((Node == NULL) || (Node->Type != AcPlistTypeBoolean))
    return EFI_INVALID_PARAMETER;
  *Value = Node->Boolean;
  return EFI_SUCCESS;
}
