/** @file
  Mutable Apple Device Tree construction and serialization.
**/

#ifndef AC_APPLE_ADT_LIB_H
#define AC_APPLE_ADT_LIB_H

#include <Uefi.h>

typedef struct AC_APPLE_ADT_TREE AC_APPLE_ADT_TREE;

/**
  Create a tree with root name "device-tree". Tree is NULL on failure.

  The tree owns its nodes and copied property data. Mutation and release require
  Boot Services in UEFI. Limits are 64 levels including the root, 65536 nodes,
  and 16 MiB of serialized data. Allocation failure leaves an existing tree
  unchanged. Invalid arguments return EFI_INVALID_PARAMETER, limits
  EFI_BAD_BUFFER_SIZE, and allocation failure EFI_OUT_OF_RESOURCES.
**/
EFI_STATUS AcAppleAdtCreate(OUT AC_APPLE_ADT_TREE **Tree);

VOID AcAppleAdtFree(IN AC_APPLE_ADT_TREE *Tree OPTIONAL);

/**
  Create missing nodes along an absolute, case-sensitive ASCII path.

  "/" is the root. Components are 1..63 printable non-space bytes, excluding
  '/' and the names "." and "..". Repeated or trailing separators are invalid.
  Existing nodes succeed without modification. New nodes are appended in path
  order; failure leaves the tree unchanged.
**/
EFI_STATUS AcAppleAdtEnsureNode(IN OUT AC_APPLE_ADT_TREE *Tree, IN CONST CHAR8 *Path);

/** Delete a subtree. Missing nodes return EFI_NOT_FOUND; root returns EFI_ACCESS_DENIED. **/
EFI_STATUS AcAppleAdtDeleteNode(IN OUT AC_APPLE_ADT_TREE *Tree, IN CONST CHAR8 *Path);

/**
  Return borrowed property bytes; clear both outputs on failure.

  Property names are 1..31 printable non-space ASCII bytes excluding '/'. Empty
  values return NULL and zero. Missing nodes or properties return EFI_NOT_FOUND.
  Data expires when replaced, deleted, or its node/tree is freed. "name" is an
  implicit NUL-terminated property maintained by node operations.
**/
EFI_STATUS
AcAppleAdtGetProperty(IN CONST AC_APPLE_ADT_TREE *Tree, IN CONST CHAR8 *Path, IN CONST CHAR8 *Name,
                      OUT CONST VOID **Data, OUT UINTN *Size);

/**
  Copy raw bytes into an existing node, replacing a same-name property in place.

  NULL Data is valid only for zero Size. Input may reference an existing value;
  it is copied before replacement. New properties are appended. "name" is
  protected and returns EFI_ACCESS_DENIED.
  Property encoding, including string NULs, is the caller's responsibility.
**/
EFI_STATUS
AcAppleAdtSetProperty(IN OUT AC_APPLE_ADT_TREE *Tree, IN CONST CHAR8 *Path, IN CONST CHAR8 *Name,
                      IN CONST VOID *Data OPTIONAL, IN UINTN Size);

/** Delete a property. Missing entries return EFI_NOT_FOUND; "name" is protected. **/
EFI_STATUS
AcAppleAdtDeleteProperty(IN OUT AC_APPLE_ADT_TREE *Tree, IN CONST CHAR8 *Path,
                         IN CONST CHAR8 *Name);

/**
  Serialize into caller-owned storage without retaining it or modifying Tree.

  NULL Buffer or insufficient capacity returns EFI_BUFFER_TOO_SMALL and the
  required byte count. On success BufferSize is the bytes written. Buffer must
  not overlap the tree's storage. Preflight errors leave Buffer unchanged.
  Encoding is little-endian with four-byte value alignment and zero padding;
  "name" is first, followed by properties and children in insertion order.
  The result has no pointers to the tree and survives AcAppleAdtFree().
**/
EFI_STATUS
AcAppleAdtSerialize(IN CONST AC_APPLE_ADT_TREE *Tree, IN OUT UINTN *BufferSize,
                    OUT VOID *Buffer OPTIONAL);

#endif
