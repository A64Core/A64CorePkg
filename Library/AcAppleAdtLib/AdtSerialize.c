/** @file
  Flattened Apple Device Tree encoding.
**/

#include "AdtInternal.h"

STATIC VOID
Write32(IN OUT UINT8 **Cursor, IN UINT32 Value)
{
  UINTN Index;

  for (Index = 0; Index < sizeof(Value); ++Index)
    *(*Cursor)++ = (UINT8)(Value >> (Index * 8));
}

STATIC VOID
WriteProperty(IN OUT UINT8 **Cursor, IN CONST CHAR8 *Name, IN CONST VOID *Data OPTIONAL,
              IN UINTN Size)
{
  CopyMem(*Cursor, Name, AsciiStrLen(Name));
  *Cursor += 32;
  Write32(Cursor, (UINT32)Size);
  if (Size != 0)
    CopyMem(*Cursor, Data, Size);
  *Cursor += ALIGN_VALUE(Size, 4);
}

STATIC VOID
WriteNode(IN OUT UINT8 **Cursor, IN CONST AC_ADT_NODE *Node)
{
  CONST AC_ADT_NODE     *Child;
  CONST AC_ADT_PROPERTY *Property;
  UINT32                 Properties = 1;
  UINT32                 Children = 0;

  for (Property = Node->Properties; Property != NULL; Property = Property->Next)
    ++Properties;
  for (Child = Node->Children; Child != NULL; Child = Child->Next)
    ++Children;
  Write32(Cursor, Properties);
  Write32(Cursor, Children);
  WriteProperty(Cursor, "name", Node->Name, AsciiStrLen(Node->Name) + 1);
  for (Property = Node->Properties; Property != NULL; Property = Property->Next)
    WriteProperty(Cursor, Property->Name, Property->Data, Property->Size);
  for (Child = Node->Children; Child != NULL; Child = Child->Next)
    WriteNode(Cursor, Child);
}

EFI_STATUS
AcAppleAdtSerialize(IN CONST AC_APPLE_ADT_TREE *Tree, IN OUT UINTN *BufferSize,
                    OUT VOID *Buffer OPTIONAL)
{
  UINTN  Capacity;
  UINT8 *Cursor = Buffer;

  if (Tree == NULL || BufferSize == NULL)
    return EFI_INVALID_PARAMETER;
  Capacity = *BufferSize;
  *BufferSize = Tree->SerializedSize;
  if (Buffer == NULL || Capacity < Tree->SerializedSize)
    return EFI_BUFFER_TOO_SMALL;
  if ((UINTN)Buffer > MAX_UINTN - Tree->SerializedSize)
    return EFI_INVALID_PARAMETER;
  ZeroMem(Buffer, Tree->SerializedSize);
  WriteNode(&Cursor, Tree->Root);
  return EFI_SUCCESS;
}
