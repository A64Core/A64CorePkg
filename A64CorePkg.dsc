## @file
# A64Core AArch64 application build.
##

[Defines]
  PLATFORM_NAME           = A64Core
  PLATFORM_GUID           = 73416504-9EE9-43E0-BB13-DCED0C98968F
  PLATFORM_VERSION        = 0.1
  DSC_SPECIFICATION       = 0x0001001B
  OUTPUT_DIRECTORY        = Build/A64Core
  SUPPORTED_ARCHITECTURES = AARCH64
  BUILD_TARGETS           = DEBUG|RELEASE|NOOPT
  SKUID_IDENTIFIER        = DEFAULT

!include MdePkg/MdeLibs.dsc.inc

[LibraryClasses]
  AcApfsLib|A64CorePkg/Library/AcApfsLib/AcApfsLib.inf
  AcAppleKernelLib|A64CorePkg/Library/AcAppleKernelLib/AcAppleKernelLib.inf
  AcBootManagementLib|A64CorePkg/Library/AcBootManagementLib/AcBootManagementLib.inf
  AcConfigurationLib|A64CorePkg/Library/AcConfigurationLib/AcConfigurationLib.inf
  AcMainLib|A64CorePkg/Library/AcMainLib/AcMainLib.inf
  AcPlistLib|A64CorePkg/Library/AcPlistLib/AcPlistLib.inf
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseCryptLib|CryptoPkg/Library/BaseCryptLib/BaseCryptLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  CacheMaintenanceLib|ArmPkg/Library/ArmCacheMaintenanceLib/ArmCacheMaintenanceLib.inf
  DebugLib|MdePkg/Library/UefiDebugLibStdErr/UefiDebugLibStdErr.inf
  DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  OpensslLib|CryptoPkg/Library/OpensslLib/OpensslLib.inf
  IntrinsicLib|CryptoPkg/Library/IntrinsicLib/IntrinsicLib.inf
  RngLib|MdePkg/Library/BaseRngLibNull/BaseRngLibNull.inf
  SynchronizationLib|MdePkg/Library/BaseSynchronizationLib/BaseSynchronizationLib.inf
  TimerLib|ArmPkg/Library/ArmArchTimerLib/ArmArchTimerLib.inf
  ArmLib|MdePkg/Library/ArmLib/ArmBaseLib.inf
  ArmGenericTimerCounterLib|ArmPkg/Library/ArmGenericTimerPhyCounterLib/ArmGenericTimerPhyCounterLib.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  UefiDriverEntryPoint|MdePkg/Library/UefiDriverEntryPoint/UefiDriverEntryPoint.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf

[Components]
  A64CorePkg/Application/Bootstrap/Bootstrap.inf
  A64CorePkg/Application/A64Core/A64Core.inf
  A64CorePkg/Library/AcPlistLib/AcPlistLib.inf
  A64CorePkg/Library/AcAppleKernelLib/AcAppleKernelLib.inf
  A64CorePkg/Library/AcApfsLib/AcApfsLib.inf
  A64CorePkg/Library/AcConfigurationLib/AcConfigurationLib.inf
  A64CorePkg/Platform/AcApfsDxe/AcApfsDxe.inf
