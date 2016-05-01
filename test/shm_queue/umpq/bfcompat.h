#ifndef _BF_COMPAT_H
#define _BF_COMPAT_H 1

#if defined(_MSC_VER)
#define ALWAYS_INLINE static __forceinline
#elif defined(__GNUC__)
#define ALWAYS_INLINE static inline
#endif

#if defined(_MSC_VER)
# include <crtdbg.h>
# define STATIC_ASSERT _STATIC_ASSERT
#elif defined(__GNUC__)
/*
 * Copyright (c) 2007, 2008, 2009, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */
/*
 * Variant based on Padraig Brady's implementation
 * http://www.pixelbeat.org/programming/gcc/static_assert.html
 */
# define ASSERT_CONCAT_(a, b) a##b
# define ASSERT_CONCAT(a, b) ASSERT_CONCAT_(a, b)
  /* This can't be used twice on the same line so ensure if using in headers
   * that the headers are not included twice (by wrapping in #ifndef...#endif)
   * Note it doesn't cause an issue when used on same line of separate modules
   * compiled with gcc -combine -fwhole-program.  */
# define STATIC_ASSERT1(e) \
    enum { ASSERT_CONCAT(assert_line_, __LINE__) = 1/(!!(e)) }
#endif

#ifndef BARRELFISH
#define ROUND_UP ROUND_UP(n, size) ((((n) + (size) - 1)) & (~((size) - 1)))
#endif



#endif // _BF_COMPAT_H
