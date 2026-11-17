/** @file
  Read-only XML property-list access.
**/

#ifndef AC_PLIST_LIB_H
#define AC_PLIST_LIB_H

#include <Uefi.h>

typedef struct AC_PLIST_DOCUMENT AC_PLIST_DOCUMENT;
typedef struct AC_PLIST_NODE     AC_PLIST_NODE;

typedef enum
{
  AcPlistTypeUnknown,
  AcPlistTypeDictionary,
  AcPlistTypeArray,
  AcPlistTypeString,
  AcPlistTypeBoolean,
  AcPlistTypeInteger,
  AcPlistTypeData,
  AcPlistTypeReal,
  AcPlistTypeDate
} AC_PLIST_TYPE;

/**
  Parse a UTF-8 XML plist without modifying or retaining Buffer.

  Document owns all nodes and decoded strings. Input need not be NUL-terminated.
  Limits are 16 MiB of input, 64 value levels, and 65536 value nodes.
  Duplicate dictionary keys and malformed XML return EFI_COMPROMISED_DATA;
  binary plists, other encodings and internal DTDs return EFI_UNSUPPORTED.
  Exceeding a limit returns EFI_BAD_BUFFER_SIZE. Allocation failure returns
  EFI_OUT_OF_RESOURCES. Document is NULL on failure.

  Integer, data, real and date nodes are recognized without value conversion.
**/
EFI_STATUS
AcPlistParse(IN CONST VOID *Buffer, IN UINTN BufferSize, OUT AC_PLIST_DOCUMENT **Document);

VOID AcPlistFree(IN AC_PLIST_DOCUMENT *Document OPTIONAL);

/**
  Export one parsed value as compact XML without a plist wrapper.
  Buffer is a FreePool-owned, NUL-terminated allocation; Size excludes its NUL.
  Scalar spelling (including negative integers) is preserved. Keys and strings
  are escaped. Outputs are cleared on failure; the parsed document is unchanged.
**/
EFI_STATUS AcPlistSerialize(IN CONST AC_PLIST_NODE *Node, OUT CHAR8 **Buffer, OUT UINTN *Size);

/** All returned nodes, keys and strings are borrowed until AcPlistFree(). **/
CONST AC_PLIST_NODE *AcPlistGetRoot(IN CONST AC_PLIST_DOCUMENT *Document);

/** NULL returns AcPlistTypeUnknown. **/
AC_PLIST_TYPE AcPlistGetType(IN CONST AC_PLIST_NODE *Node OPTIONAL);

/** Return the element or pair count; NULL and scalar nodes return zero. **/
UINTN AcPlistGetCount(IN CONST AC_PLIST_NODE *Node OPTIONAL);

/** Return NULL for a wrong type or an index past the end. **/
CONST AC_PLIST_NODE *AcPlistGetArrayItem(IN CONST AC_PLIST_NODE *Node, IN UINTN Index);

/**
  Enumerate dictionary pairs in source order. Outputs are NULL on failure.
  A wrong type returns EFI_INVALID_PARAMETER; a past-end index EFI_NOT_FOUND.
**/
EFI_STATUS
AcPlistGetDictionaryItem(IN CONST AC_PLIST_NODE *Node, IN UINTN Index, OUT CONST CHAR8 **Key,
                         OUT CONST AC_PLIST_NODE **Value);

/** Return a NUL-terminated UTF-8 string, or NULL for a wrong type. **/
CONST CHAR8 *AcPlistGetString(IN CONST AC_PLIST_NODE *Node);

/** A wrong type returns EFI_INVALID_PARAMETER. Value is FALSE on failure. **/
EFI_STATUS AcPlistGetBoolean(IN CONST AC_PLIST_NODE *Node, OUT BOOLEAN *Value);

/**
  Read an unsigned decimal or 0x-prefixed hexadecimal integer.

  XML whitespace around the value and a leading '+' are accepted; negative
  values are not. Value is zero on failure. A wrong type returns
  EFI_INVALID_PARAMETER, invalid text EFI_COMPROMISED_DATA, and overflow
  EFI_BAD_BUFFER_SIZE. No allocation is performed.
**/
EFI_STATUS AcPlistGetUnsignedInteger(IN CONST AC_PLIST_NODE *Node, OUT UINT64 *Value);

/**
  Decode a data node into a new caller-owned allocation released with FreePool().

  Base64 must have complete quartets, canonical padding and zero unused bits;
  XML whitespace is ignored. Empty data succeeds with NULL Buffer and zero Size.
  Outputs are cleared on failure. A wrong type returns EFI_INVALID_PARAMETER,
  invalid encoding EFI_COMPROMISED_DATA, and allocation failure
  EFI_OUT_OF_RESOURCES. The node and its document are unchanged.
**/
EFI_STATUS
AcPlistDecodeData(IN CONST AC_PLIST_NODE *Node, OUT UINT8 **Buffer, OUT UINTN *Size);

#endif
