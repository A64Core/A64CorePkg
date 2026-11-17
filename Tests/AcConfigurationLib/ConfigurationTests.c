/** @file
  Synthetic configuration mapping and failure-recovery tests.
**/

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Library/AcConfigurationLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>

STATIC UINTN OutstandingPools;
STATIC UINTN AllocationCalls;
STATIC UINTN FailAllocationAt;
STATIC CHAR8 LastDiagnostic[256];

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

BOOLEAN EFIAPI
DebugPrintEnabled(VOID)
{
  return TRUE;
}

BOOLEAN EFIAPI
DebugPrintLevelEnabled(CONST UINTN Level)
{
  (VOID) Level;
  return TRUE;
}

VOID EFIAPI
DebugPrint(UINTN Level, CONST CHAR8 *Format, ...)
{
  va_list      Arguments;
  CONST CHAR8 *Scope;
  CONST CHAR8 *Separator;
  CONST CHAR8 *Field;
  UINT32       Index;

  (VOID) Level;
  va_start(Arguments, Format);
  Scope = va_arg(Arguments, CONST CHAR8 *);
  if (strstr(Format, "[%u]") != NULL)
  {
    Index = va_arg(Arguments, UINT32);
    Separator = va_arg(Arguments, CONST CHAR8 *);
    Field = va_arg(Arguments, CONST CHAR8 *);
    snprintf(LastDiagnostic, sizeof(LastDiagnostic), "%s[%u]%s%s", Scope, Index, Separator, Field);
  }
  else
  {
    Field = va_arg(Arguments, CONST CHAR8 *);
    snprintf(LastDiagnostic, sizeof(LastDiagnostic), "%s%s", Scope, Field);
  }
  va_end(Arguments);
}

STATIC CONST CHAR8 Fixture[] =
  "<plist version='1.0'><dict><key>UEFI</key><dict><key>Drivers</key><array>"
  "<dict><key>Path</key><string>Normal.efi</string><key>Enabled</key><true/>"
  "<key>Arguments</key><string>one &amp; two</string><key>Comment</key><string>Example</string>"
  "</dict><dict><key>Path</key><string>Storage\\Early.efi</string>"
  "<key>Enabled</key><true/><key>LoadEarly</key><true/></dict>"
  "<dict/><dict><key>Path</key><string>Disabled.efi</string><key>Arguments</key><string/>"
  "<key>Comment</key><string/></dict>"
  "</array></dict></dict></plist>";

STATIC EFI_STATUS
Parse(CONST CHAR8 *Input, AC_GLOBAL_CONFIG *Config, UINTN *Issues)
{
  EFI_STATUS Status;

  Status = AcConfigurationInit(Input, strlen(Input), Config, Issues);
  if (EFI_ERROR(Status))
    assert((Config->Uefi.DriverCount == 0) && (Config->Uefi.Drivers == NULL));
  return Status;
}

STATIC VOID
TestOwnershipAndDefaults(VOID)
{
  AC_GLOBAL_CONFIG          Config;
  AC_UEFI_DRIVER_ENTRY     *Driver;
  CHAR8                    *Input;
  UINTN                     Issues;
  UINTN                     Index;
  STATIC CONST CHAR8 *CONST Empty[] = {
    "<plist><dict/></plist>", "<plist><dict><key>UEFI</key><dict/></dict></plist>",
    "<plist><dict><key>UEFI</key><dict><key>Drivers</key><array/></dict></dict></plist>",
    "<plist><dict><key>#Note</key><string>Example</string></dict></plist>"};

  ZeroMem(&Config, sizeof(Config));
  Input = malloc(sizeof(Fixture) - 1);
  assert(Input != NULL);
  memcpy(Input, Fixture, sizeof(Fixture) - 1);
  Issues = MAX_UINTN;
  assert(AcConfigurationInit(Input, sizeof(Fixture) - 1, &Config, &Issues) == EFI_SUCCESS);
  assert((Issues == 0) && (Config.Uefi.DriverCount == 4));
  assert(memcmp(Input, Fixture, sizeof(Fixture) - 1) == 0);
  memset(Input, 0xA5, sizeof(Fixture) - 1);
  free(Input);
  Driver = Config.Uefi.Drivers;
  assert(strcmp(Driver[0].Path, "Normal.efi") == 0);
  assert(Driver[0].Enabled && !Driver[0].LoadEarly);
  assert(strcmp(Driver[0].Arguments, "one & two") == 0);
  assert(strcmp(Driver[0].Comment, "Example") == 0);
  assert(strcmp(Driver[1].Path, "Storage\\Early.efi") == 0);
  assert(Driver[1].Enabled && Driver[1].LoadEarly);
  assert((Driver[1].Arguments == NULL) && (Driver[1].Comment == NULL));
  assert((Driver[2].Path == NULL) && !Driver[2].Enabled && !Driver[2].LoadEarly);
  assert(strcmp(Driver[3].Path, "Disabled.efi") == 0);
  assert(!Driver[3].Enabled && (Driver[3].Arguments == NULL) && (Driver[3].Comment == NULL));
  AcConfigurationFree(&Config);
  AcConfigurationFree(&Config);
  AcConfigurationFree(NULL);
  assert(OutstandingPools == 0);
  for (Index = 0; Index < ARRAY_SIZE(Empty); ++Index)
  {
    Issues = MAX_UINTN;
    assert(Parse(Empty[Index], &Config, &Issues) == EFI_SUCCESS);
    assert((Issues == 0) && (Config.Uefi.DriverCount == 0) && (Config.Uefi.Drivers == NULL));
    AcConfigurationFree(&Config);
  }
  assert(Parse(Fixture, &Config, NULL) == EFI_SUCCESS);
  AcConfigurationFree(&Config);
  assert(OutstandingPools == 0);
}

STATIC VOID
TestIssues(VOID)
{
  AC_GLOBAL_CONFIG Config;
  UINTN            Issues;
  UINTN            Index;
  STATIC           CONST struct
  {
    CONST CHAR8 *Input;
    UINTN        Issues;
  } Cases[] = {
    {"<plist><dict><key>UEFI</key><true/></dict></plist>", 1},
    {"<plist><dict><key>UEFI</key><dict><key>Drivers</key><dict/></dict></dict></plist>", 1},
    {"<plist><dict><key>Unsupported</key><dict/><key>UEFI</key><dict>"
     "<key>ConnectDrivers</key><true/></dict></dict></plist>",
     2},
    {"<plist><dict><key>UEFI</key><dict><key>Drivers</key><array>"
     "<string>Driver.efi</string><true/><dict><key>Enabled</key><true/></dict>"
     "</array></dict></dict></plist>",
     3}};
  STATIC CONST CHAR8 WrongTypes[] =
    "<plist><dict><key>UEFI</key><dict><key>Drivers</key><array><dict>"
    "<key>Path</key><true/><key>Enabled</key><string>true</string>"
    "<key>LoadEarly</key><integer>1</integer><key>Arguments</key><array/>"
    "<key>Comment</key><false/><key>Unknown</key><true/>"
    "<key>#Note</key><string>ignored</string></dict></array></dict></dict></plist>";
  STATIC CONST CHAR8 Skipped[] =
    "<plist><dict><key>UEFI</key><dict><key>Drivers</key><array>"
    "<dict><key>Path</key><string>First.efi</string><key>Enabled</key><true/></dict>"
    "<dict><key>Path</key><string>../Bad.efi</string><key>Enabled</key><true/>"
    "<key>Arguments</key><string>discarded</string></dict><false/>"
    "<dict><key>Path</key><string>Last.efi</string><key>Enabled</key><true/></dict>"
    "</array></dict></dict></plist>";

  ZeroMem(&Config, sizeof(Config));
  for (Index = 0; Index < ARRAY_SIZE(Cases); ++Index)
  {
    assert(Parse(Cases[Index].Input, &Config, &Issues) == EFI_SUCCESS);
    assert(Issues == Cases[Index].Issues);
    assert((Config.Uefi.DriverCount == 0) && (Config.Uefi.Drivers == NULL));
    AcConfigurationFree(&Config);
  }
  assert(Parse(WrongTypes, &Config, &Issues) == EFI_SUCCESS);
  assert((Issues == 6) && (Config.Uefi.DriverCount == 1));
  assert((Config.Uefi.Drivers[0].Path == NULL) && !Config.Uefi.Drivers[0].Enabled);
  assert(!Config.Uefi.Drivers[0].LoadEarly);
  assert((Config.Uefi.Drivers[0].Arguments == NULL) && (Config.Uefi.Drivers[0].Comment == NULL));
#ifndef MDEPKG_NDEBUG
  assert(strcmp(LastDiagnostic, "UEFI.Drivers[0].Unknown") == 0);
#endif
  AcConfigurationFree(&Config);
  assert(Parse(Skipped, &Config, &Issues) == EFI_SUCCESS);
  assert((Issues == 2) && (Config.Uefi.DriverCount == 2));
  assert(strcmp(Config.Uefi.Drivers[0].Path, "First.efi") == 0);
  assert(strcmp(Config.Uefi.Drivers[1].Path, "Last.efi") == 0);
#ifndef MDEPKG_NDEBUG
  assert(strcmp(LastDiagnostic, "UEFI.Drivers[2]") == 0);
#endif
  AcConfigurationFree(&Config);
  assert(OutstandingPools == 0);
}

STATIC VOID
TestPaths(VOID)
{
  AC_GLOBAL_CONFIG          Config;
  CHAR8                     Input[1024];
  UINTN                     Issues;
  UINTN                     Index;
  STATIC CONST CHAR8 *CONST Invalid[] = {"",
                                         "/Driver.efi",
                                         "\\Driver.efi",
                                         "../Driver.efi",
                                         "./Driver.efi",
                                         "Sub/../Driver.efi",
                                         "Sub\\..\\Driver.efi",
                                         "Sub/./Driver.efi",
                                         "Sub//Driver.efi",
                                         "Sub\\\\Driver.efi",
                                         "Sub/",
                                         "Disk:Driver.efi",
                                         "A&#10;B.efi",
                                         "A&#127;B.efi",
                                         "A&#128;B.efi"};
  STATIC CONST CHAR8 *CONST Valid[] = {"Driver.efi", "Sub/Driver.efi", "Sub\\Driver.efi",
                                       "\xCE\xA9.efi", ".Driver.efi"};

  ZeroMem(&Config, sizeof(Config));
  for (Index = 0; Index < ARRAY_SIZE(Invalid) + ARRAY_SIZE(Valid); ++Index)
  {
    snprintf(Input, sizeof(Input),
             "<plist><dict><key>UEFI</key><dict><key>Drivers</key>"
             "<array><dict><key>Enabled</key><true/><key>Path</key><string>%s</string>"
             "</dict></array></dict></dict></plist>",
             Index < ARRAY_SIZE(Invalid) ? Invalid[Index] : Valid[Index - ARRAY_SIZE(Invalid)]);
    assert(Parse(Input, &Config, &Issues) == EFI_SUCCESS);
    if (Index < ARRAY_SIZE(Invalid))
    {
      assert((Issues == 1) && (Config.Uefi.DriverCount == 0) && (Config.Uefi.Drivers == NULL));
#ifndef MDEPKG_NDEBUG
      assert(strcmp(LastDiagnostic, "UEFI.Drivers[0].Path") == 0);
#endif
    }
    else
      assert((Issues == 0) && (Config.Uefi.DriverCount == 1));
    AcConfigurationFree(&Config);
  }
  assert(OutstandingPools == 0);
}

STATIC VOID
TestFatalAndAllocationFailures(VOID)
{
  AC_GLOBAL_CONFIG          Config;
  UINTN                     Issues;
  UINTN                     Calls;
  UINTN                     Index;
  STATIC CONST CHAR8 *CONST Invalid[] = {
    "<plist><array/></plist>",
    "<plist><dict><key>UEFI</key><dict/><key>UEFI</key><dict/></dict></plist>",
    "<plist><dict><key>UEFI</key><dict><key>Drivers</key><array><dict>"
    "<key>Enabled</key><true/><key>Enabled</key><false/>"
    "</dict></array></dict></dict></plist>",
    "<plist><dict>"};
  STATIC CONST CHAR8 WithSkips[] =
    "<plist><dict><key>UEFI</key><dict><key>Drivers</key><array>"
    "<dict><key>Path</key><string>../Bad.efi</string><key>Enabled</key><true/>"
    "<key>Arguments</key><string>discarded</string></dict>"
    "<string>ignored</string><dict><key>Path</key><string>Good.efi</string>"
    "<key>Enabled</key><true/><key>Comment</key><string>kept</string></dict>"
    "</array></dict></dict></plist>";
  CONST CHAR8 *Fixtures[] = {Fixture, WithSkips};
  UINTN        FixtureIndex;

  ZeroMem(&Config, sizeof(Config));
  for (Index = 0; Index < ARRAY_SIZE(Invalid); ++Index)
  {
    assert(Parse(Invalid[Index], &Config, &Issues) == EFI_COMPROMISED_DATA);
    assert(OutstandingPools == 0);
  }
  Issues = MAX_UINTN;
  assert(AcConfigurationInit(NULL, 0, &Config, &Issues) == EFI_INVALID_PARAMETER);
  assert(Issues == 0);
  assert(AcConfigurationInit(Fixture, sizeof(Fixture) - 1, NULL, NULL) == EFI_INVALID_PARAMETER);
  Calls = 0;
  for (FixtureIndex = 0; FixtureIndex < ARRAY_SIZE(Fixtures); ++FixtureIndex)
  {
    AllocationCalls = 0;
    assert(Parse(Fixtures[FixtureIndex], &Config, &Issues) == EFI_SUCCESS);
    Calls = AllocationCalls;
    AcConfigurationFree(&Config);
    for (Index = 1; Index <= Calls; ++Index)
    {
      AllocationCalls = 0;
      FailAllocationAt = Index;
      assert(Parse(Fixtures[FixtureIndex], &Config, &Issues) == EFI_OUT_OF_RESOURCES);
      assert(OutstandingPools == 0);
      AcConfigurationFree(&Config);
    }
    FailAllocationAt = 0;
    assert(Parse(Fixtures[FixtureIndex], &Config, &Issues) == EFI_SUCCESS);
    AcConfigurationFree(&Config);
  }
  printf("Configuration tests passed, including allocation failure recovery");
#ifdef MDEPKG_NDEBUG
  printf(" with debug logging disabled");
#endif
  printf(".\n");
}

STATIC VOID
TestKernelAdd(VOID)
{
  STATIC CONST CHAR8 Input[] =
    "<plist><dict><key>Kernel</key><dict><key>Add</key><array>"
    "<dict><key>BundlePath</key><string>Alpha.kext</string><key>Enabled</key><true/>"
    "<key>PlistPath</key><string>Contents/Info.plist</string>"
    "<key>ExecutablePath</key><string>Contents/MacOS/Alpha</string>"
    "<key>Comment</key><string>Example &amp; test</string>"
    "<key>MinKernel</key><string>20.1.2</string><key>MaxKernel</key><string>29.99.99</string></"
    "dict>"
    "<dict><key>BundlePath</key><string>Personality.kext</string><key>Enabled</key><true/>"
    "<key>PlistPath</key><string>Contents/Info.plist</string><key>ExecutablePath</key><string/>"
    "<key>MinKernel</key><string/><key>MaxKernel</key><string/></dict>"
    "<dict><key>BundlePath</key><string>../Invalid.kext</string><key>Enabled</key><true/>"
    "<key>PlistPath</key><string>Contents/Info.plist</string></dict>"
    "<dict><key>Enabled</key><true/><key>BundlePath</key><true/></dict>"
    "<dict><key>Enabled</key><true/><key>BundlePath</key><string>Range.kext</string>"
    "<key>PlistPath</key><string>Contents/Info.plist</string><key>MinKernel</key><string>30.0.0</"
    "string>"
    "<key>MaxKernel</key><string>20.0.0</string></dict>"
    "<dict><key>Arch</key><string>Any</string></dict>"
    "</array><key>Patch</key><array/></dict></dict></plist>";
  AC_GLOBAL_CONFIG Config;
  UINTN            Issues;
  UINTN            Total;
  UINTN            Index;
  AllocationCalls = 0;
  assert(Parse(Input, &Config, &Issues) == EFI_SUCCESS);
  assert(Issues == 5 && Config.Kernel.AddCount == 6);
  assert(Config.Kernel.Add[0].Enabled && Config.Kernel.Add[1].Enabled);
  assert(strcmp(Config.Kernel.Add[0].BundlePath, "Alpha.kext") == 0);
  assert(strcmp(Config.Kernel.Add[0].Comment, "Example & test") == 0);
  assert(Config.Kernel.Add[0].MinKernel == 200102 && Config.Kernel.Add[0].MaxKernel == 299999);
  assert(Config.Kernel.Add[1].MinKernel == 0 && Config.Kernel.Add[1].MaxKernel == 0);
  assert(Config.Kernel.Add[1].ExecutablePath[0] == 0);
  for (Index = 2; Index < 6; ++Index)
    assert(!Config.Kernel.Add[Index].Enabled);
  Total = AllocationCalls;
  AcConfigurationFree(&Config);
  for (Index = 1; Index <= Total; ++Index)
  {
    AllocationCalls = 0;
    FailAllocationAt = Index;
    assert(Parse(Input, &Config, &Issues) == EFI_OUT_OF_RESOURCES);
    AcConfigurationFree(&Config);
    assert(OutstandingPools == 0);
  }
  FailAllocationAt = 0;
}

int
main(void)
{
  TestOwnershipAndDefaults();
  TestIssues();
  TestPaths();
  TestKernelAdd();
  TestFatalAndAllocationFailures();
  return 0;
}
