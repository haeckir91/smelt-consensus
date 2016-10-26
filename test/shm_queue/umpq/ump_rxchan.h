/*
 * Copyright (c) 2007, 2008, 2009, 2010, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef __UMP_RXCHAN_H
#define __UMP_RXCHAN_H

#include "ump_common.h"

/**
 * \brief Private state of a (one-way) UMP channel
 */
struct ump_rxchan {
    struct ump_message *buf;           ///< Ring buffer
    ump_index_t        pos;            ///< Current position
    ump_index_t        bufmsgs;        ///< Buffer size in messages
    bool               epoch;          ///< Next Message epoch
};

bool ump_rxchan_init(struct ump_rxchan *c, void *buf, size_t size);

/**
 * \brief Returns true iff a message is outstanding on 'c'.
 *
 * \param c     Pointer to UMP channel-state structure.
 */
ALWAYS_INLINE bool ump_rxchan_can_recv(struct ump_rxchan *c)
{
    union ump_control ctrl;
    ctrl.raw = c->buf[c->pos].control.raw;
    return (ctrl.x.epoch == c->epoch);
}

/**
 * \brief Return pointer to a message if outstanding on 'c' and
 * advance pointer.
 *
 * \param c     Pointer to UMP channel-state structure.
 *
 * \return Pointer to message if outstanding, or NULL.
 */
ALWAYS_INLINE struct ump_message *ump_rxchan_recv(struct ump_rxchan *c)
{
    union ump_control ctrl;
    struct ump_message *msg;

    ctrl.raw = c->buf[c->pos].control.raw;
    if (ctrl.x.epoch != c->epoch) {
        return NULL;
    }

    msg = &c->buf[c->pos];
    if (++c->pos == c->bufmsgs) {
        c->pos = 0;
        c->epoch = !c->epoch;
    }

    return msg;
}

#endif // __UMP_RXCHAN_H
