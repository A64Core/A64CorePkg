/** @file
  Bounded UTF-8 XML property-list parser.
**/

#include "PlistInternal.h"

typedef struct
{
  CONST UINT8       *Buffer;
  UINTN              Size;
  UINTN              Position;
  UINTN              NodeCount;
  AC_PLIST_DOCUMENT *Document;
} PLIST_PARSER;

typedef struct
{
  CONST UINT8 *Name;
  UINTN        Length;
  BOOLEAN      Empty;
} PLIST_TAG;

typedef struct
{
  CHAR8 *Buffer;
  UINTN  Length;
  UINTN  Capacity;
} PLIST_TEXT;

STATIC BOOLEAN
IsSpace(UINT8 Value)
{
  return (Value == ' ') || (Value == '\t') || (Value == '\r') || (Value == '\n');
}

STATIC BOOLEAN
IsXmlCharacter(UINT32 Value)
{
  return (Value == 9) || (Value == 10) || (Value == 13) || ((Value >= 0x20) && (Value <= 0xD7FF)) ||
         ((Value >= 0xE000) && (Value <= 0xFFFD)) || ((Value >= 0x10000) && (Value <= 0x10FFFF));
}

STATIC BOOLEAN
ValidUtf8(CONST UINT8 *Buffer, UINTN Size)
{
  UINTN  Position;
  UINTN  Count;
  UINTN  Index;
  UINT32 Value;
  UINT32 Minimum;
  UINT8  First;

  Position = 0;
  while (Position < Size)
  {
    First = Buffer[Position++];
    if (First < 0x80)
    {
      if (!IsXmlCharacter(First))
        return FALSE;
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
      return FALSE;
    if (Count > Size - Position)
      return FALSE;
    for (Index = 0; Index < Count; ++Index)
    {
      if ((Buffer[Position] & 0xC0U) != 0x80U)
        return FALSE;
      Value = (Value << 6) | (Buffer[Position++] & 0x3FU);
    }
    if ((Value < Minimum) || !IsXmlCharacter(Value))
      return FALSE;
  }
  return TRUE;
}

STATIC BOOLEAN
Starts(CONST PLIST_PARSER *Parser, CONST CHAR8 *Text)
{
  UINTN Length;

  Length = AsciiStrLen(Text);
  return (Length <= Parser->Size - Parser->Position) &&
         (CompareMem(Parser->Buffer + Parser->Position, Text, Length) == 0);
}

STATIC BOOLEAN
SliceEquals(CONST UINT8 *Text, UINTN Length, CONST CHAR8 *Expected)
{
  return (Length == AsciiStrLen(Expected)) && (CompareMem(Text, Expected, Length) == 0);
}

STATIC VOID
SkipSpace(PLIST_PARSER *Parser)
{
  while ((Parser->Position < Parser->Size) && IsSpace(Parser->Buffer[Parser->Position]))
    ++Parser->Position;
}

STATIC EFI_STATUS
SkipComment(PLIST_PARSER *Parser)
{
  Parser->Position += 4;
  while (Parser->Position < Parser->Size)
  {
    if (Starts(Parser, "-->"))
    {
      Parser->Position += 3;
      return EFI_SUCCESS;
    }
    if (Starts(Parser, "--"))
      return EFI_COMPROMISED_DATA;
    ++Parser->Position;
  }
  return EFI_COMPROMISED_DATA;
}

STATIC EFI_STATUS
SkipMisc(PLIST_PARSER *Parser)
{
  EFI_STATUS Status;

  for (;;)
  {
    SkipSpace(Parser);
    if (!Starts(Parser, "<!--"))
      return EFI_SUCCESS;
    Status = SkipComment(Parser);
    if (EFI_ERROR(Status))
      return Status;
  }
}

STATIC EFI_STATUS
ReadName(PLIST_PARSER *Parser, CONST UINT8 **Name, UINTN *Length)
{
  UINTN Start;
  UINT8 Value;

  Start = Parser->Position;
  while (Parser->Position < Parser->Size)
  {
    Value = Parser->Buffer[Parser->Position];
    if (!(((Value >= 'a') && (Value <= 'z')) || ((Value >= 'A') && (Value <= 'Z'))))
      break;
    ++Parser->Position;
  }
  *Name = Parser->Buffer + Start;
  *Length = Parser->Position - Start;
  return *Length == 0 ? EFI_COMPROMISED_DATA : EFI_SUCCESS;
}

STATIC EFI_STATUS
ReadQuoted(PLIST_PARSER *Parser, CONST UINT8 **Text, UINTN *Length)
{
  UINT8 Quote;
  UINTN Start;

  if (Parser->Position == Parser->Size)
    return EFI_COMPROMISED_DATA;
  Quote = Parser->Buffer[Parser->Position++];
  if ((Quote != '\'') && (Quote != '"'))
    return EFI_COMPROMISED_DATA;
  Start = Parser->Position;
  while ((Parser->Position < Parser->Size) && (Parser->Buffer[Parser->Position] != Quote))
  {
    if (Parser->Buffer[Parser->Position] == '<')
      return EFI_COMPROMISED_DATA;
    ++Parser->Position;
  }
  if (Parser->Position == Parser->Size)
    return EFI_COMPROMISED_DATA;
  *Text = Parser->Buffer + Start;
  *Length = Parser->Position++ - Start;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ReadAttribute(PLIST_PARSER *Parser, CONST UINT8 **Name, UINTN *NameLength, CONST UINT8 **Text,
              UINTN *TextLength)
{
  EFI_STATUS Status;

  Status = ReadName(Parser, Name, NameLength);
  if (EFI_ERROR(Status))
    return Status;
  SkipSpace(Parser);
  if (!Starts(Parser, "="))
    return EFI_COMPROMISED_DATA;
  ++Parser->Position;
  SkipSpace(Parser);
  return ReadQuoted(Parser, Text, TextLength);
}

STATIC EFI_STATUS
ReadDeclaration(PLIST_PARSER *Parser)
{
  EFI_STATUS         Status;
  CONST UINT8       *Name;
  CONST UINT8       *Text;
  UINTN              NameLength;
  UINTN              TextLength;
  UINTN              Field;
  UINTN              NextField;
  UINTN              Index;
  STATIC CONST CHAR8 Encoding[] = "utf-8";

  Parser->Position += 5;
  Field = 0;
  while (!Starts(Parser, "?>"))
  {
    if ((Parser->Position == Parser->Size) || !IsSpace(Parser->Buffer[Parser->Position]))
      return EFI_COMPROMISED_DATA;
    SkipSpace(Parser);
    if (Starts(Parser, "?>"))
      break;
    Status = ReadAttribute(Parser, &Name, &NameLength, &Text, &TextLength);
    if (EFI_ERROR(Status))
      return Status;
    if (SliceEquals(Name, NameLength, "version"))
    {
      NextField = 1;
      if (!SliceEquals(Text, TextLength, "1.0"))
        return EFI_UNSUPPORTED;
    }
    else if (SliceEquals(Name, NameLength, "encoding"))
    {
      NextField = 2;
      if (TextLength != sizeof(Encoding) - 1)
        return EFI_UNSUPPORTED;
      for (Index = 0; Index < TextLength; ++Index)
      {
        if ((Text[Index] | 0x20U) != (UINT8)Encoding[Index])
          return EFI_UNSUPPORTED;
      }
    }
    else if (SliceEquals(Name, NameLength, "standalone"))
    {
      NextField = 3;
      if (!SliceEquals(Text, TextLength, "yes") && !SliceEquals(Text, TextLength, "no"))
        return EFI_COMPROMISED_DATA;
    }
    else
      return EFI_COMPROMISED_DATA;
    if (((Field == 0) && (NextField != 1)) || (NextField <= Field))
      return EFI_COMPROMISED_DATA;
    Field = NextField;
  }
  if (Field == 0)
    return EFI_COMPROMISED_DATA;
  Parser->Position += 2;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ReadDoctype(PLIST_PARSER *Parser)
{
  EFI_STATUS   Status;
  CONST UINT8 *Text;
  UINTN        Length;
  UINTN        Count;

  Parser->Position += 9;
  if ((Parser->Position == Parser->Size) || !IsSpace(Parser->Buffer[Parser->Position]))
    return EFI_COMPROMISED_DATA;
  SkipSpace(Parser);
  Status = ReadName(Parser, &Text, &Length);
  if (EFI_ERROR(Status) || !SliceEquals(Text, Length, "plist"))
    return EFI_COMPROMISED_DATA;
  if (Starts(Parser, "["))
    return EFI_UNSUPPORTED;
  if (Starts(Parser, ">"))
  {
    ++Parser->Position;
    return EFI_SUCCESS;
  }
  if ((Parser->Position == Parser->Size) || !IsSpace(Parser->Buffer[Parser->Position]))
    return EFI_COMPROMISED_DATA;
  SkipSpace(Parser);
  if (Starts(Parser, "PUBLIC"))
  {
    Parser->Position += 6;
    Count = 2;
  }
  else if (Starts(Parser, "SYSTEM"))
  {
    Parser->Position += 6;
    Count = 1;
  }
  else
    Count = 0;
  while (Count != 0)
  {
    --Count;
    if ((Parser->Position == Parser->Size) || !IsSpace(Parser->Buffer[Parser->Position]))
      return EFI_COMPROMISED_DATA;
    SkipSpace(Parser);
    Status = ReadQuoted(Parser, &Text, &Length);
    if (EFI_ERROR(Status))
      return Status;
  }
  SkipSpace(Parser);
  if (Starts(Parser, "["))
    return EFI_UNSUPPORTED;
  if (!Starts(Parser, ">"))
    return EFI_COMPROMISED_DATA;
  ++Parser->Position;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ReadTag(PLIST_PARSER *Parser, PLIST_TAG *Tag)
{
  EFI_STATUS   Status;
  CONST UINT8 *Name;
  CONST UINT8 *Text;
  UINTN        NameLength;
  UINTN        TextLength;
  BOOLEAN      HadSpace;

  if (!Starts(Parser, "<"))
    return EFI_COMPROMISED_DATA;
  ++Parser->Position;
  Status = ReadName(Parser, &Tag->Name, &Tag->Length);
  if (EFI_ERROR(Status))
    return Status;
  HadSpace = (Parser->Position < Parser->Size) && IsSpace(Parser->Buffer[Parser->Position]);
  SkipSpace(Parser);
  if (SliceEquals(Tag->Name, Tag->Length, "plist") && !Starts(Parser, ">") && !Starts(Parser, "/>"))
  {
    if (!HadSpace)
      return EFI_COMPROMISED_DATA;
    Status = ReadAttribute(Parser, &Name, &NameLength, &Text, &TextLength);
    if (EFI_ERROR(Status) || !SliceEquals(Name, NameLength, "version"))
      return EFI_COMPROMISED_DATA;
    if (!SliceEquals(Text, TextLength, "1.0"))
      return EFI_UNSUPPORTED;
    SkipSpace(Parser);
  }
  Tag->Empty = Starts(Parser, "/>");
  if (Tag->Empty)
    Parser->Position += 2;
  else if (Starts(Parser, ">"))
    ++Parser->Position;
  else
    return EFI_COMPROMISED_DATA;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ReadCloseTag(PLIST_PARSER *Parser, CONST PLIST_TAG *Tag)
{
  EFI_STATUS   Status;
  CONST UINT8 *Name;
  UINTN        Length;

  if (!Starts(Parser, "</"))
    return EFI_COMPROMISED_DATA;
  Parser->Position += 2;
  Status = ReadName(Parser, &Name, &Length);
  if (EFI_ERROR(Status) || (Length != Tag->Length) || (CompareMem(Name, Tag->Name, Length) != 0))
    return EFI_COMPROMISED_DATA;
  SkipSpace(Parser);
  if (!Starts(Parser, ">"))
    return EFI_COMPROMISED_DATA;
  ++Parser->Position;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
AppendByte(PLIST_TEXT *Text, UINT8 Value)
{
  CHAR8 *Buffer;
  UINTN  Capacity;

  if (Text->Length + 1 >= Text->Capacity)
  {
    Capacity = Text->Capacity == 0 ? 32 : Text->Capacity * 2;
    Buffer = AllocateZeroPool(Capacity);
    if (Buffer == NULL)
      return EFI_OUT_OF_RESOURCES;
    if (Text->Buffer != NULL)
    {
      CopyMem(Buffer, Text->Buffer, Text->Length);
      FreePool(Text->Buffer);
    }
    Text->Buffer = Buffer;
    Text->Capacity = Capacity;
  }
  Text->Buffer[Text->Length++] = (CHAR8)Value;
  Text->Buffer[Text->Length] = '\0';
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
AppendCharacter(PLIST_TEXT *Text, UINT32 Value)
{
  EFI_STATUS Status;
  UINTN      Count;
  UINTN      Index;
  UINT8      Bytes[4];

  if (Value < 0x80)
  {
    Bytes[0] = (UINT8)Value;
    Count = 1;
  }
  else if (Value < 0x800)
  {
    Bytes[0] = (UINT8)(0xC0U | (Value >> 6));
    Count = 2;
  }
  else if (Value < 0x10000)
  {
    Bytes[0] = (UINT8)(0xE0U | (Value >> 12));
    Count = 3;
  }
  else
  {
    Bytes[0] = (UINT8)(0xF0U | (Value >> 18));
    Count = 4;
  }
  for (Index = 1; Index < Count; ++Index)
    Bytes[Index] = (UINT8)(0x80U | ((Value >> (6 * (Count - Index - 1))) & 0x3FU));
  for (Index = 0; Index < Count; ++Index)
  {
    Status = AppendByte(Text, Bytes[Index]);
    if (EFI_ERROR(Status))
      return Status;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ReadEntity(PLIST_PARSER *Parser, PLIST_TEXT *Text)
{
  UINT32                    Value;
  UINT32                    Base;
  UINT32                    Digit;
  UINTN                     Start;
  UINTN                     Index;
  UINT8                     Byte;
  STATIC CONST CHAR8 *CONST Names[] = {"&amp;", "&lt;", "&gt;", "&apos;", "&quot;"};
  STATIC CONST UINT8        Values[] = {'&', '<', '>', '\'', '"'};

  for (Index = 0; Index < ARRAY_SIZE(Names); ++Index)
  {
    if (Starts(Parser, Names[Index]))
    {
      Parser->Position += AsciiStrLen(Names[Index]);
      return AppendByte(Text, Values[Index]);
    }
  }
  if (!Starts(Parser, "&#"))
    return EFI_COMPROMISED_DATA;
  Parser->Position += 2;
  Base = 10;
  if (Starts(Parser, "x"))
  {
    Base = 16;
    ++Parser->Position;
  }
  Start = Parser->Position;
  Value = 0;
  while (Parser->Position < Parser->Size)
  {
    Byte = Parser->Buffer[Parser->Position];
    if ((Byte >= '0') && (Byte <= '9'))
      Digit = Byte - '0';
    else if ((Base == 16) && (Byte >= 'a') && (Byte <= 'f'))
      Digit = Byte - 'a' + 10;
    else if ((Base == 16) && (Byte >= 'A') && (Byte <= 'F'))
      Digit = Byte - 'A' + 10;
    else
      break;
    if (Value > (0x10FFFFU - Digit) / Base)
      return EFI_COMPROMISED_DATA;
    Value = Value * Base + Digit;
    ++Parser->Position;
  }
  if ((Parser->Position == Start) || !Starts(Parser, ";") || !IsXmlCharacter(Value))
    return EFI_COMPROMISED_DATA;
  ++Parser->Position;
  return AppendCharacter(Text, Value);
}

STATIC EFI_STATUS
ReadText(PLIST_PARSER *Parser, CONST PLIST_TAG *Tag, CHAR8 **Result)
{
  EFI_STATUS Status;
  PLIST_TEXT Text;
  BOOLEAN    Cdata;
  UINT8      Byte;

  ZeroMem(&Text, sizeof(Text));
  Status = EFI_SUCCESS;
  Cdata = FALSE;
  if (!Tag->Empty)
  {
    for (;;)
    {
      if (Parser->Position == Parser->Size)
      {
        Status = EFI_COMPROMISED_DATA;
        break;
      }
      if (Cdata && Starts(Parser, "]]>"))
      {
        Parser->Position += 3;
        Cdata = FALSE;
        continue;
      }
      if (!Cdata && Starts(Parser, "</"))
      {
        Status = ReadCloseTag(Parser, Tag);
        break;
      }
      if (!Cdata && Starts(Parser, "<!--"))
      {
        Status = SkipComment(Parser);
        if (EFI_ERROR(Status))
          break;
        continue;
      }
      if (!Cdata && Starts(Parser, "<![CDATA["))
      {
        Parser->Position += 9;
        Cdata = TRUE;
        continue;
      }
      Byte = Parser->Buffer[Parser->Position];
      if (!Cdata && ((Byte == '<') || Starts(Parser, "]]>")))
      {
        Status = EFI_COMPROMISED_DATA;
        break;
      }
      if (!Cdata && (Byte == '&'))
        Status = ReadEntity(Parser, &Text);
      else
      {
        ++Parser->Position;
        if (Byte == '\r')
        {
          if (Starts(Parser, "\n"))
            ++Parser->Position;
          Byte = '\n';
        }
        Status = AppendByte(&Text, Byte);
      }
      if (EFI_ERROR(Status))
        break;
    }
  }
  if (!EFI_ERROR(Status) && (Text.Buffer == NULL))
  {
    Text.Buffer = AllocateZeroPool(1);
    if (Text.Buffer == NULL)
      Status = EFI_OUT_OF_RESOURCES;
  }
  if (EFI_ERROR(Status))
  {
    if (Text.Buffer != NULL)
      FreePool(Text.Buffer);
    return Status;
  }
  *Result = Text.Buffer;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
AppendChild(AC_PLIST_NODE *Parent, AC_PLIST_NODE *Child)
{
  AC_PLIST_NODE **Children;
  UINTN           Capacity;

  if (Parent->Count == Parent->Capacity)
  {
    Capacity = Parent->Capacity == 0 ? 8 : Parent->Capacity * 2;
    Children = AllocateZeroPool(Capacity * sizeof(*Children));
    if (Children == NULL)
      return EFI_OUT_OF_RESOURCES;
    if (Parent->Children != NULL)
    {
      CopyMem(Children, Parent->Children, Parent->Count * sizeof(*Children));
      FreePool(Parent->Children);
    }
    Parent->Children = Children;
    Parent->Capacity = Capacity;
  }
  Parent->Children[Parent->Count++] = Child;
  return EFI_SUCCESS;
}

STATIC VOID
SiftKeys(AC_PLIST_NODE **Nodes, UINTN Count, UINTN Root)
{
  AC_PLIST_NODE *Value;
  UINTN          Child;

  Value = Nodes[Root];
  while (Root < Count / 2)
  {
    Child = Root * 2 + 1;
    if ((Child + 1 < Count) && (AsciiStrCmp(Nodes[Child]->Key, Nodes[Child + 1]->Key) < 0))
      ++Child;
    if (AsciiStrCmp(Value->Key, Nodes[Child]->Key) >= 0)
      break;
    Nodes[Root] = Nodes[Child];
    Root = Child;
  }
  Nodes[Root] = Value;
}

STATIC EFI_STATUS
CheckDictionary(CONST AC_PLIST_NODE *Node)
{
  AC_PLIST_NODE **Sorted;
  AC_PLIST_NODE  *Swap;
  EFI_STATUS      Status;
  UINTN           Index;

  if (Node->Count < 2)
    return EFI_SUCCESS;
  Sorted = AllocateZeroPool(Node->Count * sizeof(*Sorted));
  if (Sorted == NULL)
    return EFI_OUT_OF_RESOURCES;
  CopyMem(Sorted, Node->Children, Node->Count * sizeof(*Sorted));
  for (Index = Node->Count / 2; Index != 0; --Index)
    SiftKeys(Sorted, Node->Count, Index - 1);
  for (Index = Node->Count - 1; Index != 0; --Index)
  {
    Swap = Sorted[0];
    Sorted[0] = Sorted[Index];
    Sorted[Index] = Swap;
    SiftKeys(Sorted, Index, 0);
  }
  Status = EFI_SUCCESS;
  for (Index = 1; Index < Node->Count; ++Index)
  {
    if (AsciiStrCmp(Sorted[Index - 1]->Key, Sorted[Index]->Key) == 0)
    {
      Status = EFI_COMPROMISED_DATA;
      break;
    }
  }
  FreePool(Sorted);
  return Status;
}

STATIC EFI_STATUS
ReadNode(PLIST_PARSER *Parser, UINTN Depth, AC_PLIST_NODE **Result)
{
  EFI_STATUS                 Status;
  PLIST_TAG                  Tag;
  PLIST_TAG                  KeyTag;
  AC_PLIST_NODE             *Node;
  AC_PLIST_NODE             *Child;
  CHAR8                     *Key;
  UINTN                      Index;
  STATIC CONST CHAR8 *CONST  Names[] = {"dict",    "array", "string", "true", "false",
                                        "integer", "data",  "real",   "date"};
  STATIC CONST AC_PLIST_TYPE Types[] = {
    AcPlistTypeDictionary, AcPlistTypeArray,   AcPlistTypeString,
    AcPlistTypeBoolean,    AcPlistTypeBoolean, AcPlistTypeInteger,
    AcPlistTypeData,       AcPlistTypeReal,    AcPlistTypeDate};

  if ((Depth > AC_PLIST_MAX_DEPTH) || (Parser->NodeCount == AC_PLIST_MAX_NODES))
    return EFI_BAD_BUFFER_SIZE;
  Status = ReadTag(Parser, &Tag);
  if (EFI_ERROR(Status))
    return Status;
  for (Index = 0; Index < ARRAY_SIZE(Names); ++Index)
  {
    if (SliceEquals(Tag.Name, Tag.Length, Names[Index]))
      break;
  }
  if (Index == ARRAY_SIZE(Names))
    return EFI_COMPROMISED_DATA;
  Node = AllocateZeroPool(sizeof(*Node));
  if (Node == NULL)
    return EFI_OUT_OF_RESOURCES;
  Node->NextAllocated = Parser->Document->Nodes;
  Parser->Document->Nodes = Node;
  ++Parser->NodeCount;
  Node->Type = Types[Index];
  Node->Boolean = SliceEquals(Tag.Name, Tag.Length, "true");
  if ((Node->Type == AcPlistTypeDictionary) || (Node->Type == AcPlistTypeArray))
  {
    while (!Tag.Empty)
    {
      Status = SkipMisc(Parser);
      if (EFI_ERROR(Status))
        return Status;
      if (Starts(Parser, "</"))
      {
        Status = ReadCloseTag(Parser, &Tag);
        if (EFI_ERROR(Status))
          return Status;
        break;
      }
      Key = NULL;
      if (Node->Type == AcPlistTypeDictionary)
      {
        Status = ReadTag(Parser, &KeyTag);
        if (EFI_ERROR(Status) || !SliceEquals(KeyTag.Name, KeyTag.Length, "key"))
          return EFI_COMPROMISED_DATA;
        Status = ReadText(Parser, &KeyTag, &Key);
        if (EFI_ERROR(Status))
          return Status;
        Status = SkipMisc(Parser);
        if (EFI_ERROR(Status))
        {
          FreePool(Key);
          return Status;
        }
      }
      Status = ReadNode(Parser, Depth + 1, &Child);
      if (EFI_ERROR(Status))
      {
        if (Key != NULL)
          FreePool(Key);
        return Status;
      }
      Child->Key = Key;
      Status = AppendChild(Node, Child);
      if (EFI_ERROR(Status))
        return Status;
    }
    if (Node->Type == AcPlistTypeDictionary)
    {
      Status = CheckDictionary(Node);
      if (EFI_ERROR(Status))
        return Status;
    }
  }
  else
  {
    Status = ReadText(Parser, &Tag, &Node->Text);
    if (EFI_ERROR(Status))
      return Status;
    if ((Node->Type == AcPlistTypeBoolean) && (Node->Text[0] != '\0'))
      return EFI_COMPROMISED_DATA;
  }
  *Result = Node;
  return EFI_SUCCESS;
}

EFI_STATUS
InternalPlistParse(IN CONST UINT8 *Buffer, IN UINTN BufferSize, IN OUT AC_PLIST_DOCUMENT *Document)
{
  EFI_STATUS   Status;
  PLIST_PARSER Parser;
  PLIST_TAG    Tag;

  ZeroMem(&Parser, sizeof(Parser));
  Parser.Buffer = Buffer;
  Parser.Size = BufferSize;
  Parser.Document = Document;
  if (Starts(&Parser, "bplist") || Starts(&Parser, "\xFF\xFE") || Starts(&Parser, "\xFE\xFF") ||
      ((BufferSize >= 4) && (Buffer[0] == 0) && (Buffer[1] == 0) && (Buffer[2] == 0xFE) &&
       (Buffer[3] == 0xFF)))
    return EFI_UNSUPPORTED;
  if (!ValidUtf8(Buffer, BufferSize))
    return EFI_COMPROMISED_DATA;
  if (Starts(&Parser, "\xEF\xBB\xBF"))
    Parser.Position += 3;
  if (Starts(&Parser, "<?xml"))
  {
    Status = ReadDeclaration(&Parser);
    if (EFI_ERROR(Status))
      return Status;
  }
  Status = SkipMisc(&Parser);
  if (EFI_ERROR(Status))
    return Status;
  if (Starts(&Parser, "<!DOCTYPE"))
  {
    Status = ReadDoctype(&Parser);
    if (EFI_ERROR(Status))
      return Status;
    Status = SkipMisc(&Parser);
    if (EFI_ERROR(Status))
      return Status;
  }
  Status = ReadTag(&Parser, &Tag);
  if (EFI_ERROR(Status) || !SliceEquals(Tag.Name, Tag.Length, "plist") || Tag.Empty)
    return EFI_ERROR(Status) ? Status : EFI_COMPROMISED_DATA;
  Status = SkipMisc(&Parser);
  if (!EFI_ERROR(Status))
    Status = ReadNode(&Parser, 1, &Document->Root);
  if (!EFI_ERROR(Status))
    Status = SkipMisc(&Parser);
  if (!EFI_ERROR(Status))
    Status = ReadCloseTag(&Parser, &Tag);
  if (!EFI_ERROR(Status))
    Status = SkipMisc(&Parser);
  if (!EFI_ERROR(Status) && (Parser.Position != Parser.Size))
    Status = EFI_COMPROMISED_DATA;
  return Status;
}
