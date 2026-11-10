/** @file
  A64Core boot management library interface.
**/

#ifndef AC_BOOT_MANAGEMENT_LIB_H
#define AC_BOOT_MANAGEMENT_LIB_H

#include <Uefi.h>
#include <Library/AcConfigurationLib.h>

EFI_STATUS AcRunBootPicker(IN EFI_HANDLE ImageHandle, IN CONST AC_GLOBAL_CONFIG *Config);

#endif
