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

typedef struct
{
  CHAR8  *Buffer;
  UINTN   Used;
  BOOLEAN Failed;
} XML_WRITER;

STATIC VOID
WriteXml(XML_WRITER *Writer, CONST CHAR8 *Text, BOOLEAN Escape)
{
  CONST CHAR8 *Part;
  UINTN        Length;
  for (; *Text != '\0' && !Writer->Failed; ++Text)
  {
    Part = NULL;
    if (Escape)
    {
      if (*Text == '&')
        Part = "&amp;";
      else if (*Text == '<')
        Part = "&lt;";
      else if (*Text == '>')
        Part = "&gt;";
    }
    Length = Part == NULL ? 1 : AsciiStrLen(Part);
    if (Writer->Used > AC_PLIST_MAX_SIZE - Length)
      Writer->Failed = TRUE;
    else
    {
      if (Writer->Buffer != NULL)
        CopyMem(Writer->Buffer + Writer->Used, Part == NULL ? Text : Part, Length);
      Writer->Used += Length;
    }
  }
}

STATIC VOID
WriteXmlNode(XML_WRITER *Writer, CONST AC_PLIST_NODE *Node)
{
  STATIC CONST CHAR8 *Tags[] = {"",        "dict", "array", "string", "",
                                "integer", "data", "real",  "date"};
  UINTN               Index;
  CONST CHAR8        *Tag = Tags[Node->Type];
  if (Node->Type == AcPlistTypeBoolean)
  {
    WriteXml(Writer, Node->Boolean ? "<true/>" : "<false/>", FALSE);
    return;
  }
  WriteXml(Writer, "<", FALSE);
  WriteXml(Writer, Tag, FALSE);
  WriteXml(Writer, ">", FALSE);
  if (Node->Type == AcPlistTypeDictionary || Node->Type == AcPlistTypeArray)
  {
    for (Index = 0; Index < Node->Count; ++Index)
    {
      if (Node->Type == AcPlistTypeDictionary)
      {
        WriteXml(Writer, "<key>", FALSE);
        WriteXml(Writer, Node->Children[Index]->Key, TRUE);
        WriteXml(Writer, "</key>", FALSE);
      }
      WriteXmlNode(Writer, Node->Children[Index]);
    }
  }
  else
    WriteXml(Writer, Node->Text, TRUE);
  WriteXml(Writer, "</", FALSE);
  WriteXml(Writer, Tag, FALSE);
  WriteXml(Writer, ">", FALSE);
}

EFI_STATUS
AcPlistSerialize(IN CONST AC_PLIST_NODE *Node, OUT CHAR8 **Buffer, OUT UINTN *Size)
{
  XML_WRITER Writer = {0};
  UINTN      Length;
  if (Buffer != NULL)
    *Buffer = NULL;
  if (Size != NULL)
    *Size = 0;
  if (Node == NULL || Buffer == NULL || Size == NULL)
    return EFI_INVALID_PARAMETER;
  WriteXmlNode(&Writer, Node);
  if (Writer.Failed)
    return EFI_BAD_BUFFER_SIZE;
  Length = Writer.Used;
  Writer.Buffer = AllocateZeroPool(Length + 1);
  if (Writer.Buffer == NULL)
    return EFI_OUT_OF_RESOURCES;
  Writer.Used = 0;
  WriteXmlNode(&Writer, Node);
  *Buffer = Writer.Buffer;
  *Size = Length;
  return EFI_SUCCESS;
}
