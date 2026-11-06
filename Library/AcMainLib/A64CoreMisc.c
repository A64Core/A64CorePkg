/** @file
  A64Core miscellaneous coordination support.
**/

#include <Library/AcMainLib.h>

#include <Library/AcBootManagementLib.h>

EFI_STATUS
AcMiscBoot(IN EFI_HANDLE ImageHandle)
{
  if (ImageHandle == NULL)
    return EFI_INVALID_PARAMETER;
  return AcRunBootPicker(ImageHandle);
}
