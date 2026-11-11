/** @file
  Assertions are disabled inside the bounded third-party decoder.
**/

#ifndef AC_LZFSE_ASSERT_H
#define AC_LZFSE_ASSERT_H

#define assert(Expression) ((void)sizeof(Expression))

#endif
