/** @file
  XML property-list implementation details.
**/

#ifndef AC_PLIST_INTERNAL_H
#define AC_PLIST_INTERNAL_H

#include <Library/AcPlistLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

#define AC_PLIST_MAX_SIZE (16U * 1024U * 1024U)
#define AC_PLIST_MAX_DEPTH 64U
#define AC_PLIST_MAX_NODES 65536U

struct AC_PLIST_NODE
{
  AC_PLIST_TYPE   Type;
  BOOLEAN         Boolean;
  CHAR8          *Text;
  CHAR8          *Key;
  UINTN           Count;
  UINTN           Capacity;
  AC_PLIST_NODE **Children;
  AC_PLIST_NODE  *NextAllocated;
};

struct AC_PLIST_DOCUMENT
{
  AC_PLIST_NODE *Root;
  AC_PLIST_NODE *Nodes;
};

EFI_STATUS InternalPlistParse(IN CONST UINT8 *Buffer, IN UINTN BufferSize,
                              IN OUT AC_PLIST_DOCUMENT *Document);

#endif
