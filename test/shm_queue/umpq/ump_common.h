/*
 * Copyright (c) 2007, 2008, 2009, 2010, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef __UMP_COMMON_H
#define __UMP_COMMON_H

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "bfcompat.h"

/**
 * UMP message size should be a (small!) multiple of the hardware's
 * cache transfer size. We set this to 64 bytes, which matches the
 * coherence protocol for all x86 platforms (other than SCC). On some
 * other platforms (notably SCC and ARM) the cache transfer size is
 * only 32-bytes, but that's impractically small for most messages
 * anyway, so we still send 64-byte messages (i.e. 2 cache lines).
 */
#define UMP_MSG_BYTES      64
/// Number of words per message
#define UMP_MSG_WORDS      (UMP_MSG_BYTES / sizeof(ump_payload_word_t))
/// Number of words of payload per message (-1 for control data)
#define UMP_PAYLOAD_WORDS  (UMP_MSG_WORDS - 1)

/// We use a 32-bit control word on all platforms to enable 32<->64-bit UMP
typedef uintptr_t ump_payload_word_t;
typedef uint32_t ump_control_word_t;
STATIC_ASSERT1(sizeof(ump_control_word_t) <= sizeof(ump_payload_word_t));

#define UMP_EPOCH_BITS  1
#define UMP_HEADER_BITS (sizeof(ump_control_word_t) * CHAR_BIT - UMP_EPOCH_BITS)

union ump_control {
	struct {
		ump_control_word_t epoch:UMP_EPOCH_BITS;
		ump_control_word_t header:UMP_HEADER_BITS;
	} x;
	ump_control_word_t raw;
};
STATIC_ASSERT1(sizeof(union ump_control) == sizeof(ump_control_word_t));

struct ump_message {
    union ump_control control;
    ump_payload_word_t data[UMP_PAYLOAD_WORDS];
};
STATIC_ASSERT1(sizeof(struct ump_message) == UMP_MSG_BYTES);

/// Type used for indices of UMP message slots. This limits the maximum size of a channel.
typedef uint16_t ump_index_t;
#define UMP_INDEX_BITS         (sizeof(ump_index_t) * CHAR_BIT)
#define UMP_INDEX_MASK         ((((uintptr_t)1) << UMP_INDEX_BITS) - 1)

/// Emit memory barrier needed between writing UMP payload and header
ALWAYS_INLINE void ump_write_barrier(void)
{
#if defined(__i386__) || defined(__x86_64__) || defined(__scc__) || defined(_M_X64) || defined(_M_IX86)
    /* the x86 memory model ensures ordering of stores, so all we need to do
     * is prevent the compiler from reordering the instructions */
# if defined(__GNUC__)
    __asm volatile ("" : : : "memory");
# elif defined(_MSC_VER)
    _WriteBarrier();
# else
# error force ordering here!
# endif
#else // !x86 (TODO: the x86 optimisation may be applicable to other architectures)
# ifdef __GNUC__
    /* use conservative GCC intrinsic */
    __sync_synchronize();
# else
#  error memory barrier here!
# endif
#endif
}

/// compare and swap helper for sleep/wakeup
ALWAYS_INLINE bool ump_cas(int volatile *p, int old, int new_)
{
#if defined(__GNUC__)
    return __sync_bool_compare_and_swap(p, old, new_);
#elif defined(_MSC_VER)
    return _InterlockedCompareExchange(p, new_, old) == old;
#else
# error implement CAS here
#endif
}

#endif // __UMP_COMMON_H
