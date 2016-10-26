/*
 * Copyright (c) 2010, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef __UMP_CHAN_H
#define __UMP_CHAN_H

#include "stdbool.h" // Private copy

#include "ump_txchan.h"
#include "ump_rxchan.h"

/// Number of bits available for the message type in the header
#define UMP_MSGTAG_BITS (UMP_HEADER_BITS - UMP_INDEX_BITS)

/// Special message tags
enum ump_msgtag {
    UMP_ACK_MSGTAG = (1 << UMP_MSGTAG_BITS) - 1,
};

/// Wait/wake state of a bidirectional UMP channel
typedef enum {UMP_RUNNING = 0, UMP_WAIT_0, UMP_WAIT_1} ump_chan_wake_state_t;

/// Shared state (read/write to both sides) of a bidirectional UMP channel
struct ump_chan_shared {
    volatile int32_t wake_state; // ump_chan_wake_state_t
};

/**
 * \brief Private state of a bidirectional UMP channel
 */
struct ump_chan {
    struct ump_chan_shared *shared; ///< Pointer to shared state
    struct ump_txchan tx;
    struct ump_rxchan rx;
    bool discriminant;    ///< True on one side, false on the other

    void *peer_wake_context;

    ump_index_t next_id;   ///< Sequence number of next message to be sent
    ump_index_t seq_id;    ///< Last sequence number received from remote
    ump_index_t ack_id;    ///< Last sequence number acknowledged by remote
    ump_index_t last_ack;  ///< Last acknowledgement we sent to remote
};

bool ump_chan_init(struct ump_chan *c, void *shmem, size_t shmem_bytes,
                   bool discriminant, void *peer_wake_context);
bool ump_chan_init_numa(struct ump_chan *c, struct ump_chan_shared *shared,
                        void *local_shmem, size_t local_shmem_bytes,
                        void *remote_shmem, size_t remote_shmem_bytes,
                        bool discriminant, void *peer_wake_context);
void ump_chan_tx_submit(struct ump_chan *c, struct ump_message *msg,
                        unsigned msgtag);
bool ump_chan_send_ack(struct ump_chan *c);
struct ump_message *ump_chan_rx(struct ump_chan *c, unsigned *ret_msgtag);
bool ump_chan_try_wait(struct ump_chan *c);
void ump_chan_wakeup(struct ump_chan *c);


ALWAYS_INLINE void ump_chan_tx_submit_zero(struct ump_chan *chan)
{
    union ump_control ctrl;

    //ctrl.x.header = ((uintptr_t)msgtag << UMP_INDEX_BITS) | (uintptr_t)c->seq_id;
    ctrl.x.header = (uintptr_t)chan->seq_id;
    chan->last_ack = chan->seq_id;
    chan->next_id++;

    ump_txchan_submit_zero(&chan->tx, ctrl);
}

/// Computes (from seq/ack numbers) whether we can currently send on the channel
ALWAYS_INLINE bool ump_chan_can_send(struct ump_chan *c)
{
    return (ump_index_t)(c->next_id - c->ack_id) <= c->tx.bufmsgs;
}

ALWAYS_INLINE bool ump_chan_rx_zero(struct ump_chan *c)
{
    struct ump_message *msg;

    msg = ump_rxchan_recv(&c->rx);
    if (msg == NULL) {
        return false;
    }

    c->ack_id = msg->control.x.header & UMP_INDEX_MASK;
    c->seq_id++;

    unsigned msgtag = msg->control.x.header >> UMP_INDEX_BITS;
    if (msgtag == UMP_ACK_MSGTAG) {
        return false;
    }
    return true;

}

/// Checks (polls) for the presence of incoming messages
ALWAYS_INLINE bool ump_chan_can_recv(struct ump_chan *c)
{
    return ump_rxchan_can_recv(&c->rx);
}

/**
 * \brief Begin the process of transmitting a message on the given channel
 *
 * The channel must ready to accept a message. The caller should fill out the 
 * message's payload ('data' array). The message is not sent
 * until a subsequent call to ump_chan_tx_submit().
 */
ALWAYS_INLINE struct ump_message *ump_chan_tx_prepare(struct ump_chan *c)
{
    assert(ump_chan_can_send(c));
    return ump_txchan_prepare(&c->tx);
}

/// Should we send an ACK?
ALWAYS_INLINE bool ump_chan_needs_ack(struct ump_chan *s)
{
    // send a forced ACK if the channel is full
    // FIXME: should probably send it only when "nearly" full
    return (ump_index_t)(s->seq_id - s->last_ack) >=
        (ump_index_t)(s->rx.bufmsgs - 1);
}

#endif // __UMP_CHAN_H
