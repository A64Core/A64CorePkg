/** @file
  Platform configuration mapping.
**/

#include "ConfigurationInternal.h"

STATIC EFI_STATUS
ParseCpuHandoff(CONST AC_PLIST_NODE *Node, AC_CPU_HANDOFF_CONFIG *Handoff, UINTN *Issues)
{
  CONST AC_PLIST_NODE *Value;
  CONST CHAR8         *Key;
  CONST CHAR8         *Text;

  if (AcPlistGetType(Node) != AcPlistTypeDictionary)
  {
    InternalConfigurationIssue(Issues, "Platform.", MAX_UINTN, "CpuHandoff",
                               "expected a dictionary");
    Handoff->Mode = AcCpuHandoffInvalid;
    return EFI_SUCCESS;
  }
  for (UINTN Index = 0; Index < AcPlistGetCount(Node); ++Index)
  {
    AcPlistGetDictionaryItem(Node, Index, &Key, &Value);
    if (Key[0] == '#')
      continue;
    if (AsciiStrCmp(Key, "Mode") == 0)
    {
      Text = AcPlistGetString(Value);
      if (Text == NULL || AsciiStrCmp(Text, "Direct") != 0)
      {
        InternalConfigurationIssue(Issues, "Platform.CpuHandoff.", MAX_UINTN, Key, "invalid mode");
        Handoff->Mode = AcCpuHandoffInvalid;
      }
    }
    else
      InternalConfigurationIssue(Issues, "Platform.CpuHandoff.", MAX_UINTN, Key, "unsupported key");
  }
  return EFI_SUCCESS;
}

STATIC VOID
ParseFramebuffer(CONST AC_PLIST_NODE *Node, AC_FRAMEBUFFER_CONFIG *Framebuffer, UINTN *Issues)
{
  CONST AC_PLIST_NODE *Value;
  CONST CHAR8         *Key;
  CONST CHAR8         *Text;
  UINT64               Number;
  UINT64              *Wide;
  UINT32              *Narrow;
  UINT32               Present = 0;
  UINT32               Bit;
  EFI_STATUS           Status;
  BOOLEAN              Valid = TRUE;

  if (AcPlistGetType(Node) != AcPlistTypeDictionary)
  {
    InternalConfigurationIssue(Issues, "Platform.", MAX_UINTN, "Framebuffer",
                               "expected a dictionary");
    Framebuffer->Mode = AcFramebufferInvalid;
    return;
  }
  for (UINTN Index = 0; Index < AcPlistGetCount(Node); ++Index)
  {
    AcPlistGetDictionaryItem(Node, Index, &Key, &Value);
    if (Key[0] == '#')
      continue;
    Wide = NULL;
    Narrow = NULL;
    Bit = 0;
    Status = EFI_SUCCESS;
    if (AsciiStrCmp(Key, "Mode") == 0)
    {
      Text = AcPlistGetString(Value);
      if (Text != NULL && AsciiStrCmp(Text, "Auto") == 0)
        Framebuffer->Mode = AcFramebufferAuto;
      else if (Text != NULL && AsciiStrCmp(Text, "Override") == 0)
        Framebuffer->Mode = AcFramebufferOverride;
      else
        Status = EFI_INVALID_PARAMETER;
    }
    else if (AsciiStrCmp(Key, "PixelFormat") == 0)
    {
      Text = AcPlistGetString(Value);
      if (Text == NULL || AsciiStrCmp(Text, "BGRX8") != 0)
        Status = EFI_UNSUPPORTED;
      Bit = BIT0;
    }
    else if (AsciiStrCmp(Key, "Address") == 0)
    {
      Wide = &Framebuffer->Address;
      Bit = BIT1;
    }
    else if (AsciiStrCmp(Key, "Size") == 0)
    {
      Wide = &Framebuffer->Size;
      Bit = BIT2;
    }
    else if (AsciiStrCmp(Key, "Width") == 0)
    {
      Narrow = &Framebuffer->Width;
      Bit = BIT3;
    }
    else if (AsciiStrCmp(Key, "Height") == 0)
    {
      Narrow = &Framebuffer->Height;
      Bit = BIT4;
    }
    else if (AsciiStrCmp(Key, "RowBytes") == 0)
    {
      Narrow = &Framebuffer->RowBytes;
      Bit = BIT5;
    }
    else if (AsciiStrCmp(Key, "Rotation") == 0)
    {
      Narrow = &Framebuffer->Rotation;
      Bit = BIT6;
    }
    else
    {
      InternalConfigurationIssue(Issues, "Platform.Framebuffer.", MAX_UINTN, Key,
                                 "unsupported key");
      continue;
    }
    if (Wide != NULL || Narrow != NULL)
    {
      Status = AcPlistGetUnsignedInteger(Value, &Number);
      if (!EFI_ERROR(Status))
      {
        if (Wide != NULL)
          *Wide = Number;
        else if (Number <= MAX_UINT32)
          *Narrow = (UINT32)Number;
        else
          Status = EFI_BAD_BUFFER_SIZE;
      }
    }
    if (EFI_ERROR(Status))
    {
      InternalConfigurationIssue(Issues, "Platform.Framebuffer.", MAX_UINTN, Key,
                                 "invalid field value");
      Valid = FALSE;
    }
    else
      Present |= Bit;
  }
  if (Framebuffer->Mode == AcFramebufferOverride &&
      (Present != 0x7F || Framebuffer->Address == 0 || (Framebuffer->Address & 3) != 0 ||
       Framebuffer->Size == 0 || Framebuffer->Address > MAX_UINT64 - Framebuffer->Size ||
       Framebuffer->Width == 0 || Framebuffer->Height == 0 || (Framebuffer->RowBytes & 3) != 0 ||
       (UINT64)Framebuffer->RowBytes < (UINT64)Framebuffer->Width * 4 ||
       (UINT64)Framebuffer->RowBytes * Framebuffer->Height > Framebuffer->Size ||
       Framebuffer->Rotation > 270 || Framebuffer->Rotation % 90 != 0))
  {
    InternalConfigurationIssue(Issues, "Platform.", MAX_UINTN, "Framebuffer", "invalid override");
    Valid = FALSE;
  }
  if (!Valid)
    Framebuffer->Mode = AcFramebufferInvalid;
}

EFI_STATUS
InternalConfigurationParsePlatform(IN CONST AC_PLIST_NODE    *Node,
                                   IN OUT AC_PLATFORM_CONFIG *Platform, IN OUT UINTN *IssueCount)
{
  CONST AC_PLIST_NODE *Value;
  CONST CHAR8         *Key;

  if (AcPlistGetType(Node) != AcPlistTypeDictionary)
  {
    InternalConfigurationIssue(IssueCount, "", MAX_UINTN, "Platform", "expected a dictionary");
    Platform->Framebuffer.Mode = AcFramebufferInvalid;
    Platform->CpuHandoff.Mode = AcCpuHandoffInvalid;
    return EFI_SUCCESS;
  }
  for (UINTN Index = 0; Index < AcPlistGetCount(Node); ++Index)
  {
    AcPlistGetDictionaryItem(Node, Index, &Key, &Value);
    if (Key[0] == '#')
      continue;
    if (AsciiStrCmp(Key, "Framebuffer") == 0)
      ParseFramebuffer(Value, &Platform->Framebuffer, IssueCount);
    else if (AsciiStrCmp(Key, "CpuHandoff") == 0)
    {
      EFI_STATUS Status = ParseCpuHandoff(Value, &Platform->CpuHandoff, IssueCount);
      if (EFI_ERROR(Status))
        return Status;
    }
    else
      InternalConfigurationIssue(IssueCount, "Platform.", MAX_UINTN, Key, "unsupported key");
  }
  return EFI_SUCCESS;
}
