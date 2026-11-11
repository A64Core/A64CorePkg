/** @file
  Map third-party memory operations to EDK II.
**/

#ifndef AC_LZFSE_STRING_H
#define AC_LZFSE_STRING_H

#include <Library/BaseMemoryLib.h>

#define memcpy(Destination, Source, Size) CopyMem((Destination), (Source), (Size))
#define memset(Destination, Value, Size) SetMem((Destination), (Size), (UINT8)(Value))

#endif
