/*
 * Copyright (c) 2007, 2008, 2009, 2010, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef __UMP_TXCHAN_H
#define __UMP_TXCHAN_H

#include "ump_common.h"

/**
 * \brief Private state of a (one-way) UMP transmit channel
 */
struct ump_txchan {
    struct ump_message *buf;           ///< Ring buffer
    ump_index_t        pos;            ///< Current position
    ump_index_t        bufmsgs;        ///< Buffer size in messages
    bool               epoch;          ///< Next Message epoch
};

bool ump_txchan_init(struct ump_txchan *c, void *buf, size_t size);

/**
 * \brief Determine next position for an outgoing message on a channel.
 *
 * \param c     Pointer to UMP channel-state structure.
 *
 * \return Pointer to message slot
 */
ALWAYS_INLINE struct ump_message *ump_txchan_prepare(struct ump_txchan *c)
{
    return &c->buf[c->pos];
}

ALWAYS_INLINE void ump_txchan_submit_zero(struct ump_txchan *c,
                                          union ump_control ctrl)
{
    // write control word (thus sending the message)
    ctrl.x.epoch = c->epoch;
    c->buf[c->pos].control.raw = ctrl.raw;

    // update pos
    if (++c->pos == c->bufmsgs) {
        c->pos = 0;
        c->epoch = !c->epoch;
    }
}


/**
 * \brief Determine next position for an outgoing message on a channel, and
 *   advance send pointer.
 *
 * \param c     Pointer to UMP channel-state structure.
 * \param msg   Pointer to next message slot in channel.
 * \param ctrl  Partial control word for next message
 *
 * \return Pointer to message slot
 */
ALWAYS_INLINE void ump_txchan_submit(struct ump_txchan *c,
                                     struct ump_message *msg,
                                     union ump_control ctrl)
{
    // must submit in order
    assert(msg == &c->buf[c->pos]);

    // write barrier for message payload
    ump_write_barrier();

    // write control word (thus sending the message)
    ctrl.x.epoch = c->epoch;
    msg->control.raw = ctrl.raw;

    // update pos
    if (++c->pos == c->bufmsgs) {
        c->pos = 0;
        c->epoch = !c->epoch;
    }
}

#endif // __UMP_TXCHAN_H
