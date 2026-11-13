/** @file
  Fixed-width ARM64 Apple boot argument layouts.
**/

#ifndef APPLE_BOOT_H
#define APPLE_BOOT_H

#include <Base.h>

#define AC_APPLE_BOOT_LINE_LENGTH 1024U
#define AC_APPLE_BOOT_FLAG_DARK (1ULL << 0)
#define AC_APPLE_BOOT_VIDEO_ROTATION_SHIFT 8U
// initialize_screen() uses bit zero to request mapping a physical aperture.
#define AC_APPLE_BOOT_VIDEO_PHYSICAL 1ULL

typedef struct
{
  UINT64 BaseAddress;
  UINT64 Display;
  UINT64 RowBytes;
  UINT64 Width;
  UINT64 Height;
  UINT64 Depth;
} AC_APPLE_BOOT_VIDEO;

typedef struct
{
  UINT64 PhysicalAddress;
  UINT64 Length;
} AC_APPLE_BOOT_MEMORY_RANGE;

typedef struct
{
  UINT16              Revision;
  UINT16              Version;
  UINT32              Reserved0;
  UINT64              VirtBase;
  UINT64              PhysBase;
  UINT64              MemSize;
  UINT64              TopOfKernelData;
  AC_APPLE_BOOT_VIDEO Video;
  UINT32              MachineType;
  UINT32              Reserved1;
  UINT64              DeviceTree;
  UINT32              DeviceTreeLength;
  CHAR8               CommandLine[AC_APPLE_BOOT_LINE_LENGTH];
  UINT32              Reserved2;
  UINT64              BootFlags;
  UINT64              MemSizeActual;
} AC_APPLE_BOOT_ARGS;

#endif
