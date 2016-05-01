#include <stdio.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "ump_chan.h"
#include "ump_queue.h"
#include "ump_os.h"
#include "misc.h"

//#define DEBUG_ON 1

#ifdef DEBUG_ON
#define DEBUG(...) printf(__VA_ARGS__)
#else
#define DEBUG(...) ((void)0)
#endif

#if defined(__GNUC__)
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif

void ump_queue_init(struct ump_queue *q, struct ump_chan *chan, void *sleep_event)
{
    q->chan = chan;
    q->sleep_event = sleep_event;
}

/// cycle counter for polling
typedef uint64_t cycles_t;
static cycles_t cyclecount(void)
{
#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
    uint32_t eax, edx;
    __asm volatile ("rdtsc" : "=a" (eax), "=d" (edx));
    return ((uint64_t)edx << 32) | eax;
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    return __rdtsc();
#else
# error TODO: cycle counter
#endif
}

/// How long to busy-wait before blocking
#define POLLCYCLES 20000

static bool can_enqueue(struct ump_queue *q)
{
    struct ump_chan *chan = q->chan;
    bool saw_ack = false;

    if (ump_chan_can_send(chan)) {
        return true;
    }

    // process acks
    while (ump_chan_can_recv(chan)) {

        struct ump_message UNUSED *msg = ump_chan_rx(chan, NULL);
        assert(msg == NULL); // just an ack
        assert(ump_chan_can_send(chan)); // should now have space in the channel
        saw_ack = true;
    }

    return saw_ack;
}

bool ump_can_dequeue(struct ump_queue *q) 
{
    struct ump_chan *chan = q->chan;
    return ump_chan_can_recv(chan);
}


bool ump_enqueue_word_nonblock(struct ump_queue *q, uintptr_t val)
{
    struct ump_chan *chan = q->chan;
    
    /* if (measure_send) { sk_m_restart_tsc(&send); } */
    
    if (can_enqueue(q)) {
        struct ump_message *msg;
        msg = ump_chan_tx_prepare(chan);
        msg->data[0] = val;

        ump_chan_tx_submit(chan, msg, 0 /* msgtag */);
        /* if (measure_send) { sk_m_add(&send); } */

        return true;
    } else {
        return false; // would block
    }
}


bool ump_enqueue_nonblock(struct ump_queue *q, 
                          uintptr_t val1,
                          uintptr_t val2,
                          uintptr_t val3,
                          uintptr_t val4,
                          uintptr_t val5,
                          uintptr_t val6,
                          uintptr_t val7)
{
    struct ump_chan *chan = q->chan;
    
    /* if (measure_send) { sk_m_restart_tsc(&send); } */
    
    if (can_enqueue(q)) {
        struct ump_message *msg;
        msg = ump_chan_tx_prepare(chan);
        msg->data[0] = val1;
        msg->data[1] = val2;
        msg->data[2] = val3;
        msg->data[3] = val4;
        msg->data[4] = val5;
        msg->data[5] = val6;
        msg->data[6] = val7;

        ump_chan_tx_submit(chan, msg, 0 /* msgtag */);
        /* if (measure_send) { sk_m_add(&send); } */

        return true;
    } else {
        return false; // would block
    }
}

/**
 * \brief Enqueue word
 *
 * This is a blocking operationg. If no message can currently be sent
 * on the channel, we listen on the reverse channels for ACKs. After
 * some polling, we sleep until receiving a message on the ACK
 * channel.
 */
void ump_enqueue_word(struct ump_queue *q, uintptr_t val)
{
    bool could_enqueue, UNUSED success;
    struct ump_chan *chan = q->chan;

    // try to send once, directly
    if (ump_enqueue_word_nonblock(q, val)) {
        return;
    }

    do {
        // busy-wait on channel, until timeout
        cycles_t timeout = cyclecount() + POLLCYCLES;
        do {
            could_enqueue = can_enqueue(q);
        } while (!could_enqueue && (cyclecount() < timeout || !ump_sleep_enabled()));

        // if we timed out, try to block
        if (!could_enqueue && ump_chan_try_wait(chan)) {
            ump_sleep(q->sleep_event);
            ump_chan_wakeup(chan);
            could_enqueue = can_enqueue(q);
        } else if (!could_enqueue) {
            DEBUG("enqueue: failed to sleep\n");
        }
    } while (!could_enqueue);

    success = ump_enqueue_word_nonblock(q, val);
    assert(success);
}

void ump_enqueue(struct ump_queue *q, 
                 uintptr_t val1,
                 uintptr_t val2,
                 uintptr_t val3,
                 uintptr_t val4,
                 uintptr_t val5,
                 uintptr_t val6,
                 uintptr_t val7)
{
    bool could_enqueue, UNUSED success;
    struct ump_chan *chan = q->chan;

    // try to send once, directly
    if (ump_enqueue_nonblock(q, val1, val2, val3, 
                             val4, val5, val6, val7)) {
        return;
    }

    do {
        // busy-wait on channel, until timeout
        cycles_t timeout = cyclecount() + POLLCYCLES;
        do {
            could_enqueue = can_enqueue(q);
        } while (!could_enqueue && (cyclecount() < timeout || !ump_sleep_enabled()));

        // if we timed out, try to block
        if (!could_enqueue && ump_chan_try_wait(chan)) {
            ump_sleep(q->sleep_event);
            ump_chan_wakeup(chan);
            could_enqueue = can_enqueue(q);
        } else if (!could_enqueue) {
            DEBUG("enqueue: failed to sleep\n");
        }
    } while (!could_enqueue);

    success = ump_enqueue_nonblock(q, val1, val2, val3, val4,
                                        val5, val6, val7);
    assert(success);
}


bool ump_dequeue_word_nonblock(struct ump_queue *q, uintptr_t *ret)
{
    struct ump_message *msg;
    struct ump_chan *chan = q->chan;

    msg = ump_chan_rx(chan, NULL);
    if (msg == NULL) {
        return false; // would block
    }

    if (ret != NULL) {
        *ret = msg->data[0];
    }

    if (ump_chan_needs_ack(chan)) {
        bool UNUSED success = ump_chan_send_ack(chan);
        assert(success);
    }

    return true;
}

/**
 * \brief Dequeue
 *  
 *  The dequeue functions assume that ret is backed up
 *  with memory for up to the payload of a ump_message.
 */
bool ump_dequeue_nonblock(struct ump_queue *q, uintptr_t *ret)
{
    struct ump_message *msg;
    struct ump_chan *chan = q->chan;

    msg = ump_chan_rx(chan, NULL);
    if (msg == NULL) {
        return false; // would block
    }

    if (ret != NULL) {
        ret[0] = msg->data[0];
        ret[1] = msg->data[1];
        ret[2] = msg->data[2];
        ret[3] = msg->data[3];
        ret[4] = msg->data[4];
        ret[5] = msg->data[5];
        ret[6] = msg->data[6];
    }

    if (ump_chan_needs_ack(chan)) {
        bool UNUSED success = ump_chan_send_ack(chan);
        assert(success);
    }

    return true;
}


void ump_dequeue_word(struct ump_queue *q, uintptr_t *ret)
{
    
    bool success;
    struct ump_chan *chan = q->chan;

    // So the fast path here is REALLY fast, seems to be triggered for
    // about 80-90% of the executions. We still have a high
    // standard-error here, but the numbers seem reasonable.
    if (ump_dequeue_word_nonblock(q, ret)) {
        return;
    }

    do {
        // busy-wait on channel, until timeout
        cycles_t timeout = cyclecount() + POLLCYCLES;
        do {
            success = ump_dequeue_word_nonblock(q, ret);
        } while (!success && (cyclecount() < timeout || !ump_sleep_enabled()));

        // if we timed out, try to block
        if (!success && ump_chan_try_wait(chan)) {
            ump_sleep(q->sleep_event);
            ump_chan_wakeup(chan);
            success = ump_dequeue_word_nonblock(q, ret);
        } else if (!success) {
            DEBUG("dequeue: failed to sleep\n");
        }
    } while (!success);
    

}

void ump_dequeue(struct ump_queue *q, uintptr_t *ret)
{
    
    bool success;
    struct ump_chan *chan = q->chan;

    // So the fast path here is REALLY fast, seems to be triggered for
    // about 80-90% of the executions. We still have a high
    // standard-error here, but the numbers seem reasonable.
    if (ump_dequeue_nonblock(q, ret)) {
        return;
    }

    do {
        // busy-wait on channel, until timeout
        cycles_t timeout = cyclecount() + POLLCYCLES;
        do {
            success = ump_dequeue_nonblock(q, ret);
        } while (!success && (cyclecount() < timeout || !ump_sleep_enabled()));

        // if we timed out, try to block
        if (!success && ump_chan_try_wait(chan)) {
            ump_sleep(q->sleep_event);
            ump_chan_wakeup(chan);
            success = ump_dequeue_nonblock(q, ret);
        } else if (!success) {
            DEBUG("dequeue: failed to sleep\n");
        }
    } while (!success);
    

}
