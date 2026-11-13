/** @file
  Apple Device Tree construction implementation details.
**/

#ifndef AC_ADT_INTERNAL_H
#define AC_ADT_INTERNAL_H

#include <Library/AcAppleAdtLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

#define AC_ADT_MAX_SIZE (16U * 1024U * 1024U)
#define AC_ADT_MAX_DEPTH 64U
#define AC_ADT_MAX_NODES 65536U
#define AC_ADT_PROPERTY_HEADER_SIZE 36U
#define AC_ADT_NODE_HEADER_SIZE 8U

typedef struct AC_ADT_PROPERTY
{
  struct AC_ADT_PROPERTY *Next;
  UINTN                   Size;
  CHAR8                   Name[32];
  UINT8                   Data[];
} AC_ADT_PROPERTY;

typedef struct AC_ADT_NODE
{
  struct AC_ADT_NODE *Next;
  struct AC_ADT_NODE *Children;
  AC_ADT_PROPERTY    *Properties;
  CHAR8               Name[64];
} AC_ADT_NODE;

struct AC_APPLE_ADT_TREE
{
  AC_ADT_NODE *Root;
  UINTN        SerializedSize;
  UINT32       NodeCount;
};

#endif
