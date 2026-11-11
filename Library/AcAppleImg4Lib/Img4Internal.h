/** @file
  Private bounded DER readers.
**/

#ifndef AC_IMG4_INTERNAL_H
#define AC_IMG4_INTERNAL_H

#include <Library/AcAppleImg4Lib.h>
#include <Library/BaseMemoryLib.h>

typedef struct
{
  CONST UINT8 *Data;
  UINTN        Size;
} AC_DER_CURSOR;

typedef struct
{
  UINT8        Tag;
  CONST UINT8 *Data;
  UINTN        Size;
} AC_DER_ITEM;

EFI_STATUS InternalDerRead(IN OUT AC_DER_CURSOR *Cursor, OUT AC_DER_ITEM *Item);
EFI_STATUS InternalDerInteger(IN CONST AC_DER_ITEM *Item, OUT UINT64 *Value);
EFI_STATUS InternalDerValidate(IN AC_DER_CURSOR Cursor, IN UINTN Depth, IN OUT UINTN *Count);

#endif
