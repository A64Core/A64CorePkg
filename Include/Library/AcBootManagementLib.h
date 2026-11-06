/** @file
  A64Core boot management library interface.
**/

#ifndef AC_BOOT_MANAGEMENT_LIB_H
#define AC_BOOT_MANAGEMENT_LIB_H

#include <Uefi.h>

EFI_STATUS AcRunBootPicker(IN EFI_HANDLE ImageHandle);

#endif
