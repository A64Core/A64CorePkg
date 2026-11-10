/** @file
  A64Core primary coordination library interface.
**/

#ifndef AC_MAIN_LIB_H
#define AC_MAIN_LIB_H

#include <Uefi.h>

#include <Library/AcConfigurationLib.h>

/**
  Read EFI\AC\config.plist from ImageHandle's source file system.

  Config must be zero-initialized or previously freed. It is cleared on failure.
  The caller releases a successful result with AcConfigurationFree().
**/
EFI_STATUS AcLoadConfiguration(IN EFI_HANDLE ImageHandle, OUT AC_GLOBAL_CONFIG *Config);

/**
  Start enabled drivers in the requested phase, preserving configuration order.

  Config is borrowed and must come from AcConfigurationInit(). Paths are relative
  to EFI\AC\Drivers on ImageHandle's source partition. Individual driver failures
  are diagnosed and skipped; allocation failure aborts the phase. Successful
  images and their independently allocated load options remain resident.
**/
EFI_STATUS
AcLoadDrivers(IN EFI_HANDLE ImageHandle, IN CONST AC_GLOBAL_CONFIG *Config, IN BOOLEAN LoadEarly);

/** Recursively connect device roots using the firmware's existing driver policy. **/
EFI_STATUS AcConnectDrivers(VOID);

/**
  Enter boot management.

  Config is borrowed for the duration of the call.

  @return  The status returned by the boot picker.
**/
EFI_STATUS
AcMiscBoot(IN EFI_HANDLE ImageHandle, IN CONST AC_GLOBAL_CONFIG *Config);

#endif
