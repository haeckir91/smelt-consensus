#include "ump_chan.h"
#include "ump_os.h"


#define ALIGN_UP(n, size)       ((((n) + (size) - 1)) & (~((size) - 1)))
#define ALIGN_DOWN(n, size)     ((n) & (~((size) - 1)))

/**
 * \brief Initialize the private state of a UMP channel
 *
 * This should be called once on each endpoint of the channel.
 *
 * \param c        Pointer to channel-state structure to initialize.
 * \param shmem    Shared memory to be used by the channel.
 * \param shmem_size  Size (in bytes) of shared memory region.
 * \param discriminant  Allows us to distinguish the two endpoints.
 *                 Must be passed as true on one and false on the other.
 * \param peer_wake_context  Opaque parameter to peer notification function
 *
 * \returns true on success, false on failure (invalid parameters)
 */
bool ump_chan_init(struct ump_chan *c, void *shmem, size_t shmem_bytes,
                   bool discriminant, void *peer_wake_context)
{
    struct ump_chan_shared *shared;
    void *buf1, *buf2;
    size_t size;

    shared = (struct ump_chan_shared*) shmem;
    assert(((uintptr_t)shared % sizeof(uintptr_t)) == 0);

    // align remaining shared memory and split it in half, for each direction
    shmem = (void *)ALIGN_UP((uintptr_t)(shared + 1), UMP_MSG_BYTES);
    assert(((uintptr_t)shmem) - ((uintptr_t)shared) < shmem_bytes);
    size = (shmem_bytes - ((uintptr_t)shmem - (uintptr_t)shared)) / 2;
    size = ALIGN_DOWN(size, UMP_MSG_BYTES);
    if (discriminant) {
        buf1 = shmem;
        buf2 = (char *)shmem + size;
    } else {
        buf1 = (char *)shmem + size;
        buf2 = shmem;
    }

    return ump_chan_init_numa(c, shared, buf1, size, buf2, size, discriminant,
                              peer_wake_context);
}

/**
 * \brief Initialize the private state of a UMP channel (NUMA-aware version)
 *
 * This should be called once on each side of the channel. It permits
 * splitting the send channel and receive channel across two NUMA domains.
 *
 * \param c       Pointer to private channel-state structure to initialize.
 * \param shared  Pointer to shared channel state, to be initialised.
 * \param local_shmem       Local shared memory to be used by the channel.
 *                          Must be mapped, and aligned to a cacheline.
 * \param local_shmem_bytes Size (in bytes) of local shared memory region.
 *                          Must be multiple of #UMP_MSG_BYTES.
 * \param remote_shmem      Remote shared memory to be used by the channel.
 *                          Must be mapped, and aligned to a cacheline.
 * \param remote_shmem_bytes Size (in bytes) of remote shared memory region.
 *                          Must be multiple of #UMP_MSG_BYTES.
 * \param discriminant  Allows us to distinguish the two endpoints.
 *                 Must be passed as true on one and false on the other.
 * \param peer_wake_context  Opaque parameter to peer notification function
 *
 * \returns true on success, false on failure (invalid parameters)
 */
bool ump_chan_init_numa(struct ump_chan *c, struct ump_chan_shared *shared,
                        void *local_shmem, size_t local_shmem_bytes,
                        void *remote_shmem, size_t remote_shmem_bytes,
                        bool discriminant, void *peer_wake_context)
{
    // Use local memory for tx, remote for rx.  For MOESI (AMD) with a
    // directory/snoop filter it shouldn't make a difference, but for
    // MESI or MESI-F (Intel) the modified line always has to be
    // written back on the core doing the tx, and it's better to do
    // that writeback on the local memory controller.

    // TODO: validate this guess with measurements!

    if (!ump_txchan_init(&c->tx, local_shmem, local_shmem_bytes)) {
        return false;
    }

    if (!ump_rxchan_init(&c->rx, remote_shmem, remote_shmem_bytes)) {
        return false;
    }

    c->shared = shared;
    c->discriminant = discriminant;
    c->peer_wake_context = peer_wake_context;

    c->next_id = 1;
    c->seq_id = 0;
    c->ack_id = 0;
    c->last_ack = 0;

    // XXX: both peers set the channel state to running
    // This works only because of the order of channel initialisation
    // on Barrelfish: nobody attempts to receive before both sides have
    // initialised their end of the channel. There is a similar problem
    // with the channel itself -- see ump_txchan_init(). We should resolve
    // this with a separate API that initialises the shared part of the channel
    // and is called once only, before the other peer initialises.
    shared->wake_state = UMP_RUNNING;

    return true;
}

ALWAYS_INLINE void ump_chan_tx_submit_unchecked(struct ump_chan *c,
                                         struct ump_message *msg,
                                         unsigned msgtag)
{
    union ump_control ctrl;
    ump_chan_wake_state_t wake_state;

    ctrl.x.header = ((uintptr_t)msgtag << UMP_INDEX_BITS) | (uintptr_t)c->seq_id;
    c->last_ack = c->seq_id;
    c->next_id++;

    ump_txchan_submit(&c->tx, msg, ctrl);

    // do we need to wake the other side?
    wake_state = (ump_chan_wake_state_t) c->shared->wake_state;
    if (wake_state != UMP_RUNNING) {
        // we shouldn't be asleep if we're sending!
        assert(wake_state == c->discriminant ? UMP_WAIT_1 : UMP_WAIT_0);
        ump_wake_peer(c->peer_wake_context);
    }
}

/**
 * \brief Send a message previously prepared with ump_chan_tx_prepare()
 *
 * \param c      Local channel pointer
 * \param msg    Message body. Must have been returned by a previous call to
 *               ump_chan_tx_prepare().
 * \param msgtag Tag to be delivered with message payload.
 *               Must be < #UMP_ACK_MSGTAG.
 */
void ump_chan_tx_submit(struct ump_chan *c, struct ump_message *msg,
                        unsigned msgtag)
{
    assert(msgtag < UMP_ACK_MSGTAG); // check for overflow
    ump_chan_tx_submit_unchecked(c, msg, msgtag);
}


bool ump_chan_send_ack(struct ump_chan *c)
{
    if (ump_chan_can_send(c)) {
        struct ump_message *msg = ump_chan_tx_prepare(c);
        ump_chan_tx_submit_unchecked(c, msg, UMP_ACK_MSGTAG);
        return true;
    } else {
        return false;
    }
}

/**
 * \brief Retrieve an incoming message, if present
 *
 * \param c           Local channel pointer
 * \param ret_msgtag  Optional pointer to return message tag.
 *
 * \returns Pointer to message on success, NULL if no message is present.
 *
 * Upon successful return, the next call to ump_chan_tx_submit() or
 * ump_chan_send_ack() will send an acknowledgement covering this message. So,
 * the caller must ensure that its payload is consumed or copied to stable
 * storage beforehand.
 */
struct ump_message *ump_chan_rx(struct ump_chan *c, unsigned *ret_msgtag)
{
    struct ump_message *msg;
    unsigned msgtag;

    msg = ump_rxchan_recv(&c->rx);
    if (msg == NULL) {
        return NULL;
    }

    c->ack_id = msg->control.x.header & UMP_INDEX_MASK;
    msgtag = msg->control.x.header >> UMP_INDEX_BITS;
    c->seq_id++;
    if (msgtag == UMP_ACK_MSGTAG) {
        return NULL;
    }

    if (ret_msgtag != NULL) {
        *ret_msgtag = msgtag;
    }

    return msg;
}

/**
 * \brief Try to wait (stop polling and block for notification)
 *
 * \returns True on success
 *
 * The channel must be empty (not possible to receive). When this call
 * succeeds, this end of the channel is considered to be asleep, and
 * it is an error to call any functions other than ump_chan_wake().
 */
bool ump_chan_try_wait(struct ump_chan *c)
{
    ump_chan_wake_state_t wait_state = c->discriminant ? UMP_WAIT_0 : UMP_WAIT_1;

    // check before waiting
    if (ump_chan_can_recv(c)) {
        return false;
    }

    // try to CAS to the waiting state
    if (ump_cas(&c->shared->wake_state, UMP_RUNNING, wait_state)) {
        // CAS succeeded, but check again
        if (ump_chan_can_recv(c)) {
            // a message arrived while we were doing the CAS; wake up again
            c->shared->wake_state = UMP_RUNNING;
            return false;
        } else {
            return true; // blocked!
        }
    }

    return false;
}

/**
 * \brief Wakeup and return to polling
 *
 * The preceding call to ump_chan_try_wait() must have succeeded.
 */
void ump_chan_wakeup(struct ump_chan *c)
{
    assert(c->shared->wake_state == c->discriminant ? UMP_WAIT_0 : UMP_WAIT_1);
    c->shared->wake_state = UMP_RUNNING;
}

void ump_end(void)
{
}

void ump_start(void)
{
}
