/** @file
  Synthetic XML plist parsing and ownership tests.
**/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Library/AcPlistLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

STATIC UINTN OutstandingPools;
STATIC UINTN AllocationCalls;
STATIC UINTN FailAllocationAt;

VOID *EFIAPI
CopyMem(VOID *Destination, CONST VOID *Source, UINTN Length)
{
  return memmove(Destination, Source, Length);
}

VOID *EFIAPI
ZeroMem(VOID *Buffer, UINTN Length)
{
  return memset(Buffer, 0, Length);
}

INTN EFIAPI
CompareMem(CONST VOID *First, CONST VOID *Second, UINTN Length)
{
  return memcmp(First, Second, Length);
}

UINTN EFIAPI
AsciiStrLen(CONST CHAR8 *Text)
{
  return strlen(Text);
}

INTN EFIAPI
AsciiStrCmp(CONST CHAR8 *First, CONST CHAR8 *Second)
{
  return strcmp(First, Second);
}

VOID *EFIAPI
AllocateZeroPool(UINTN Size)
{
  VOID *Buffer;

  ++AllocationCalls;
  if ((FailAllocationAt != 0) && (AllocationCalls == FailAllocationAt))
    return NULL;
  Buffer = calloc(1, Size);
  if (Buffer != NULL)
    ++OutstandingPools;
  return Buffer;
}

VOID EFIAPI
FreePool(VOID *Buffer)
{
  assert((Buffer != NULL) && (OutstandingPools != 0));
  --OutstandingPools;
  free(Buffer);
}

STATIC CONST CHAR8 Fixture[] =
  "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
  "<!-- prolog --><!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
  "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">"
  "<plist version='1.0'><dict>"
  "<key>Z</key><string> leading &amp;&lt;&gt;&apos;&quot; &#937;&#x1F600; trailing </string>"
  "<key>A</key><array><true/><false></false><string/><dict/><array/>"
  "<integer>42</integer><data>AAEC</data><real>1.5</real><date>2000-01-01T00:00:00Z</date>"
  "</array><key/><string>one\r\ntwo\rthree\n<!-- gap -->"
  "<![CDATA[<&amp;\r\n]]>&#13;&#xA;</string>"
  "</dict></plist><!-- epilog -->";

STATIC VOID
Walk(CONST AC_PLIST_NODE *Node)
{
  CONST AC_PLIST_NODE *Value;
  CONST CHAR8         *Key;
  UINTN                Index;
  BOOLEAN              Boolean;

  assert(Node != NULL);
  for (Index = 0; Index < AcPlistGetCount(Node); ++Index)
  {
    if (AcPlistGetType(Node) == AcPlistTypeDictionary)
    {
      assert(AcPlistGetDictionaryItem(Node, Index, &Key, &Value) == EFI_SUCCESS);
      assert(Key != NULL);
      (VOID) strlen(Key);
    }
    else
      Value = AcPlistGetArrayItem(Node, Index);
    Walk(Value);
  }
  if (AcPlistGetType(Node) == AcPlistTypeString)
    assert(AcPlistGetString(Node) != NULL);
  if (AcPlistGetType(Node) == AcPlistTypeBoolean)
    assert(AcPlistGetBoolean(Node, &Boolean) == EFI_SUCCESS);
}

STATIC VOID
TestDocument(VOID)
{
  AC_PLIST_DOCUMENT         *Document;
  CONST AC_PLIST_NODE       *Root;
  CONST AC_PLIST_NODE       *Value;
  CONST AC_PLIST_NODE       *Array;
  CONST CHAR8               *Key;
  CHAR8                     *Input;
  BOOLEAN                    Boolean;
  UINTN                      Index;
  STATIC CONST AC_PLIST_TYPE Types[] = {
    AcPlistTypeBoolean,    AcPlistTypeBoolean, AcPlistTypeString,
    AcPlistTypeDictionary, AcPlistTypeArray,   AcPlistTypeInteger,
    AcPlistTypeData,       AcPlistTypeReal,    AcPlistTypeDate};

  Input = malloc(sizeof(Fixture) - 1);
  assert(Input != NULL);
  memcpy(Input, Fixture, sizeof(Fixture) - 1);
  assert(AcPlistParse(Input, sizeof(Fixture) - 1, &Document) == EFI_SUCCESS);
  assert(memcmp(Input, Fixture, sizeof(Fixture) - 1) == 0);
  memset(Input, 0xA5, sizeof(Fixture) - 1);
  free(Input);
  Root = AcPlistGetRoot(Document);
  assert(AcPlistGetType(Root) == AcPlistTypeDictionary);
  assert(AcPlistGetCount(Root) == 3);
  assert(AcPlistGetDictionaryItem(Root, 0, &Key, &Value) == EFI_SUCCESS);
  assert(strcmp(Key, "Z") == 0);
  assert(strcmp(AcPlistGetString(Value), " leading &<>'\" \xCE\xA9\xF0\x9F\x98\x80 trailing ") ==
         0);
  assert(AcPlistGetDictionaryItem(Root, 1, &Key, &Array) == EFI_SUCCESS);
  assert(strcmp(Key, "A") == 0);
  assert(AcPlistGetCount(Array) == ARRAY_SIZE(Types));
  for (Index = 0; Index < ARRAY_SIZE(Types); ++Index)
    assert(AcPlistGetType(AcPlistGetArrayItem(Array, Index)) == Types[Index]);
  assert(AcPlistGetBoolean(AcPlistGetArrayItem(Array, 0), &Boolean) == EFI_SUCCESS);
  assert(Boolean);
  assert(AcPlistGetBoolean(AcPlistGetArrayItem(Array, 1), &Boolean) == EFI_SUCCESS);
  assert(!Boolean);
  assert(strcmp(AcPlistGetString(AcPlistGetArrayItem(Array, 2)), "") == 0);
  assert(AcPlistGetDictionaryItem(Root, 2, &Key, &Value) == EFI_SUCCESS);
  assert(Key[0] == '\0');
  assert(strcmp(AcPlistGetString(Value), "one\ntwo\nthree\n<&amp;\n\r\n") == 0);
  Walk(Root);

  Key = (CONST CHAR8 *)1;
  Value = (CONST AC_PLIST_NODE *)1;
  assert(AcPlistGetDictionaryItem(Root, 3, &Key, &Value) == EFI_NOT_FOUND);
  assert((Key == NULL) && (Value == NULL));
  assert(AcPlistGetDictionaryItem(Array, 0, &Key, &Value) == EFI_INVALID_PARAMETER);
  assert(AcPlistGetDictionaryItem(Root, 0, NULL, &Value) == EFI_INVALID_PARAMETER);
  assert(Value == NULL);
  assert(AcPlistGetArrayItem(Root, 0) == NULL);
  assert(AcPlistGetArrayItem(Array, MAX_UINTN) == NULL);
  assert(AcPlistGetString(Array) == NULL);
  Boolean = TRUE;
  assert(AcPlistGetBoolean(Array, &Boolean) == EFI_INVALID_PARAMETER);
  assert(!Boolean);
  assert(AcPlistGetBoolean(Root, NULL) == EFI_INVALID_PARAMETER);
  assert(AcPlistGetType(NULL) == AcPlistTypeUnknown);
  assert(AcPlistGetCount(NULL) == 0);
  assert(AcPlistGetRoot(NULL) == NULL);
  AcPlistFree(Document);
  AcPlistFree(NULL);
  assert(OutstandingPools == 0);
}

STATIC VOID
Expect(CONST VOID *Buffer, UINTN Size, EFI_STATUS Expected)
{
  AC_PLIST_DOCUMENT *Document;
  EFI_STATUS         Status;

  Document = (AC_PLIST_DOCUMENT *)1;
  Status = AcPlistParse(Buffer, Size, &Document);
  if (Status != Expected)
  {
    fprintf(stderr, "Unexpected parse status: 0x%llx, expected 0x%llx\n",
            (unsigned long long)Status, (unsigned long long)Expected);
    abort();
  }
  if (EFI_ERROR(Status))
    assert(Document == NULL);
  else
  {
    Walk(AcPlistGetRoot(Document));
    AcPlistFree(Document);
  }
  assert(OutstandingPools == 0);
}

STATIC VOID
TestInvalid(VOID)
{
  UINTN                     Index;
  STATIC CONST CHAR8 *CONST Invalid[] = {
    "",
    "<dict/>",
    "<plist/>",
    "<plist></plist>",
    "<plist><dict/></plist>x",
    "<plist><dict/><dict/></plist>",
    "<plist><array></dict></plist>",
    "<plist><dict><key>A</key></dict></plist>",
    "<plist><dict><key>A</key><!-- unfinished",
    "<plist><dict><string>A</string><true/></dict></plist>",
    "<plist><array><key>A</key></array></plist>",
    "<plist><dict><key>A</key><true/><key>&#65;</key><false/></dict></plist>",
    "<plist><dict><key>A</key><true/><key>A</key><false/></dict></plist>",
    "<plist><string>&unknown;</string></plist>",
    "<plist><string>&#0;</string></plist>",
    "<plist><string>&#xD800;</string></plist>",
    "<plist><string>&#x110000;</string></plist>",
    "<plist><string>&#x;</string></plist>",
    "<plist><string>&#999999999999999999999;</string></plist>",
    "<plist><string>&#12;</string></plist>",
    "<plist><string>&#-1;</string></plist>",
    "<plist><string>&amp</string></plist>",
    "<plist><string>]]></string></plist>",
    "<plist><string><![CDATA[unterminated</string></plist>",
    "<plist><string><true/></string></plist>",
    "<plist><true>yes</true></plist>",
    "<plist><dict/><!-- a--b --></plist>",
    "<plist><dict/><!-- a---></plist>",
    "<plist><dict/><!-- missing</plist>",
    "<plist><DICT/></plist>",
    "<plist><dict extra='x'/></plist>",
    "<plist version='1.0' version='1.0'><dict/></plist>",
    "<plistversion='1.0'><dict/></plist>",
    "<plist><unknown/></plist>",
    "<?xml?><plist><dict/></plist>",
    "<?xml encoding='UTF-8'?><plist><dict/></plist>",
    "<?xml version='1.0' version='1.0'?><plist><dict/></plist>",
    "<?xml version='1.0'encoding='UTF-8'?><plist><dict/></plist>",
    " <?xml version='1.0'?><plist><dict/></plist>",
    "<!DOCTYPE other><plist><dict/></plist>",
    "<!DOCTYPEplist><plist><dict/></plist>",
    "<!DOCTYPE plist PUBLIC 'one'><plist><dict/></plist>",
    "<!DOCTYPE plist><!DOCTYPE plist><plist><dict/></plist>",
    "<plist><string>\xC0\xAF</string></plist>",
    "<plist><string>\xED\xA0\x80</string></plist>",
    "<plist><string>\xF4\x90\x80\x80</string></plist>",
    "<plist><string>\xEF\xBF\xBE</string></plist>",
    "<plist><string>\xE2\x82</string></plist>"};
  STATIC CONST CHAR8 *CONST Unsupported[] = {
    "bplist00",
    "<?xml version='1.1'?><plist><dict/></plist>",
    "<?xml version='1.0' encoding='UTF-16'?><plist><dict/></plist>",
    "<plist version='2.0'><dict/></plist>",
    "<!DOCTYPE plist [<!ENTITY a 'value'>]><plist><string>&a;</string></plist>",
    "<!DOCTYPE plist[]><plist><dict/></plist>"};
  STATIC CONST UINT8 Utf16[] = {0xFF, 0xFE, '<', 0};
  STATIC CONST UINT8 Utf32[] = {0, 0, 0xFE, 0xFF};
  STATIC CONST CHAR8 EmbeddedNul[] = "<plist><string>a\0b</string></plist>";
  STATIC CONST CHAR8 SystemDoctype[] = "<!DOCTYPE plist SYSTEM 'unused'><plist><string/></plist>";

  for (Index = 0; Index < ARRAY_SIZE(Invalid); ++Index)
    Expect(Invalid[Index], strlen(Invalid[Index]), EFI_COMPROMISED_DATA);
  for (Index = 0; Index < ARRAY_SIZE(Unsupported); ++Index)
    Expect(Unsupported[Index], strlen(Unsupported[Index]), EFI_UNSUPPORTED);
  Expect(Utf16, sizeof(Utf16), EFI_UNSUPPORTED);
  Expect(Utf32, sizeof(Utf32), EFI_UNSUPPORTED);
  Expect(EmbeddedNul, sizeof(EmbeddedNul) - 1, EFI_COMPROMISED_DATA);
  Expect(NULL, 0, EFI_INVALID_PARAMETER);
  Expect(Fixture, 16U * 1024U * 1024U + 1, EFI_BAD_BUFFER_SIZE);
  assert(AcPlistParse(Fixture, sizeof(Fixture), NULL) == EFI_INVALID_PARAMETER);
  Expect(SystemDoctype, sizeof(SystemDoctype) - 1, EFI_SUCCESS);
}

STATIC VOID
TestLimits(VOID)
{
  CHAR8 *Buffer;
  UINTN  Position;
  UINTN  Depth;
  UINTN  Index;
  UINTN  Count;

  Buffer = malloc(600000);
  assert(Buffer != NULL);
  for (Depth = 64; Depth <= 65; ++Depth)
  {
    Position = (UINTN)snprintf(Buffer, 600000, "<plist>");
    for (Index = 1; Index < Depth; ++Index)
      Position += (UINTN)snprintf(Buffer + Position, 600000 - Position, "<array>");
    Position += (UINTN)snprintf(Buffer + Position, 600000 - Position, "<string/>");
    for (Index = 1; Index < Depth; ++Index)
      Position += (UINTN)snprintf(Buffer + Position, 600000 - Position, "</array>");
    Position += (UINTN)snprintf(Buffer + Position, 600000 - Position, "</plist>");
    Expect(Buffer, Position, Depth == 64 ? EFI_SUCCESS : EFI_BAD_BUFFER_SIZE);
  }
  for (Count = 65535; Count <= 65536; ++Count)
  {
    Position = (UINTN)snprintf(Buffer, 600000, "<plist><array>");
    for (Index = 0; Index < Count; ++Index)
      Position += (UINTN)snprintf(Buffer + Position, 600000 - Position, "<true/>");
    Position += (UINTN)snprintf(Buffer + Position, 600000 - Position, "</array></plist>");
    Expect(Buffer, Position, Count == 65535 ? EFI_SUCCESS : EFI_BAD_BUFFER_SIZE);
  }
  Position = (UINTN)snprintf(Buffer, 600000, "<plist><dict>");
  for (Index = 0; Index < 2048; ++Index)
    Position += (UINTN)snprintf(Buffer + Position, 600000 - Position, "<key>k%04u</key><true/>",
                                (unsigned)Index);
  Position += (UINTN)snprintf(Buffer + Position, 600000 - Position, "</dict></plist>");
  Expect(Buffer, Position, EFI_SUCCESS);
  Position -= strlen("</dict></plist>");
  Position += (UINTN)snprintf(Buffer + Position, 600000 - Position,
                              "<key>k0000</key><false/></dict></plist>");
  Expect(Buffer, Position, EFI_COMPROMISED_DATA);
  free(Buffer);
}

STATIC VOID
TestFailuresAndMutations(VOID)
{
  AC_PLIST_DOCUMENT *Document;
  EFI_STATUS         Status;
  CHAR8             *Input;
  UINTN              Calls;
  UINTN              Index;
  UINTN              Size;
  UINT32             Random;

  AllocationCalls = 0;
  assert(AcPlistParse(Fixture, sizeof(Fixture) - 1, &Document) == EFI_SUCCESS);
  Calls = AllocationCalls;
  AcPlistFree(Document);
  for (Index = 1; Index <= Calls; ++Index)
  {
    AllocationCalls = 0;
    FailAllocationAt = Index;
    Expect(Fixture, sizeof(Fixture) - 1, EFI_OUT_OF_RESOURCES);
  }
  FailAllocationAt = 0;
  for (Size = 0; Size < sizeof(Fixture) - 1; ++Size)
  {
    Input = malloc(Size == 0 ? 1 : Size);
    assert(Input != NULL);
    memcpy(Input, Fixture, Size);
    Status = AcPlistParse(Input, Size, &Document);
    if (!EFI_ERROR(Status))
      AcPlistFree(Document);
    else
      assert(Document == NULL);
    free(Input);
    assert(OutstandingPools == 0);
  }
  Random = 7;
  for (Index = 0; Index < 2000; ++Index)
  {
    Input = malloc(sizeof(Fixture) - 1);
    assert(Input != NULL);
    memcpy(Input, Fixture, sizeof(Fixture) - 1);
    Random = Random * 1664525U + 1013904223U;
    Size = Random % (sizeof(Fixture) - 1);
    Random = Random * 1664525U + 1013904223U;
    Input[Size] = (CHAR8)(Random >> 24);
    Status = AcPlistParse(Input, sizeof(Fixture) - 1, &Document);
    free(Input);
    if (!EFI_ERROR(Status))
    {
      Walk(AcPlistGetRoot(Document));
      AcPlistFree(Document);
    }
    else
      assert(Document == NULL);
    assert(OutstandingPools == 0);
  }
  printf("Plist tests passed: %u allocation failures, %u truncations, 2000 mutations.\n",
         (unsigned)Calls, (unsigned)(sizeof(Fixture) - 1));
}

STATIC VOID
TestSerialization(VOID)
{
  AC_PLIST_DOCUMENT *Document;
  AC_PLIST_DOCUMENT *RoundTrip;
  CHAR8             *Xml;
  CHAR8             *Again;
  CHAR8             *Wrapped;
  UINTN              Size;
  UINTN              AgainSize;
  STATIC CONST CHAR8 Text[] =
    "<plist><dict><key>A&amp;&lt;</key><array><integer>-42</integer><data>AAEC</data>"
    "<real>1.5</real><date>2000-01-01T00:00:00Z</date><true/><false/>"
    "<string>&lt;&gt;&amp; Ω</string><dict/></array></dict></plist>";
  assert(AcPlistParse(Text, sizeof(Text) - 1, &Document) == EFI_SUCCESS);
  assert(AcPlistSerialize(AcPlistGetRoot(Document), &Xml, &Size) == EFI_SUCCESS);
  assert(Xml[Size] == 0 && strstr(Xml, "<integer>-42</integer>") != NULL);
  assert(strstr(Xml, "<key>A&amp;&lt;</key>") != NULL);
  Wrapped = malloc(Size + 16);
  assert(Wrapped != NULL);
  snprintf(Wrapped, Size + 16, "<plist>%s</plist>", Xml);
  assert(AcPlistParse(Wrapped, strlen(Wrapped), &RoundTrip) == EFI_SUCCESS);
  assert(AcPlistSerialize(AcPlistGetRoot(RoundTrip), &Again, &AgainSize) == EFI_SUCCESS);
  assert(Size == AgainSize && strcmp(Xml, Again) == 0);
  FreePool(Again);
  AcPlistFree(RoundTrip);
  free(Wrapped);
  FreePool(Xml);
  AllocationCalls = 0;
  FailAllocationAt = 1;
  assert(AcPlistSerialize(AcPlistGetRoot(Document), &Xml, &Size) == EFI_OUT_OF_RESOURCES);
  assert(Xml == NULL && Size == 0);
  FailAllocationAt = 0;
  AcPlistFree(Document);
  assert(OutstandingPools == 0);
}

int
main(void)
{
  TestDocument();
  TestInvalid();
  TestLimits();
  TestSerialization();
  TestFailuresAndMutations();
  return 0;
}
