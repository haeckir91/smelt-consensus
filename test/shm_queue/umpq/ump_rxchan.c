/*
 * Copyright (c) 2007, 2008, 2009, 2010, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#define __STDC_LIMIT_MACROS
#include <stdint.h>

#include "ump_rxchan.h"
#include "ump_common.h"

/**
 * \brief Initialize UMP receive channel state
 *
 * The channel-state structure and buffer must already be allocated.
 *
 * \param       c       Pointer to channel-state structure to initialize.
 * \param       buf     Pointer to ring buffer for the channel.
 *                      Must be aligned to #UMP_MSG_BYTES
 * \param       size    Size (in bytes) of buffer.
 *                      Must be multiple of #UMP_MSG_BYTES
 *
 * \returns true on success, false on failure (invalid parameters)
 */
bool ump_rxchan_init(struct ump_rxchan *c, void *buf, size_t size)
{
    // check alignment and size of buffer.
    if (size == 0 || (size % UMP_MSG_BYTES) != 0
        || (size / UMP_MSG_BYTES) > UINT16_MAX) {
        return false;
    }

    if (buf == NULL || (((uintptr_t)buf) % UMP_MSG_BYTES) != 0) {
        return false;
    }

    c->pos = 0;
    c->buf = (struct ump_message*) buf;
    c->bufmsgs = size / UMP_MSG_BYTES;
    c->epoch = 1;

    return true;
}
