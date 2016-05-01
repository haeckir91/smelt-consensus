#ifndef __UMP_QUEUE_H
#define __UMP_QUEUE_H

struct ump_chan;
struct ump_queue {
    struct ump_chan *chan;
    void *sleep_event;
};


ALWAYS_INLINE volatile bool ump_enqueue_zero_nonblock(struct ump_queue *q)
{
   struct ump_chan *chan = q->chan;

   ump_chan_tx_submit_zero(chan);
   return true;
    if (ump_chan_can_send(chan)) {
        ump_chan_tx_submit_zero(chan);
        return true;
    } else {
        return false; // would block
    }
}

ALWAYS_INLINE volatile bool ump_dequeue_zero_nonblock(struct ump_queue *q)
{
    struct ump_chan *chan = q->chan;
    bool msg =  ump_chan_rx_zero(chan);
    if (!msg) {
       return false;
    }
    #if 0
    if (ump_chan_needs_ack(chan))  {
        ump_chan_send_ack(chan);
    }
    #endif
    return true;
}

ALWAYS_INLINE bool ump_enqueue_zero(struct ump_queue *q)
{
    bool enqueued;
    do {
      enqueued = ump_enqueue_zero_nonblock(q);
    } while(!enqueued);

    return true;
}

ALWAYS_INLINE bool ump_dequeue_zero(struct ump_queue *q)
{
    bool dequeued;
    do {
      dequeued = ump_dequeue_zero_nonblock(q);
    } while(!dequeued);

    return true;
}


void ump_queue_init(struct ump_queue *q, struct ump_chan *chan, void *sleep_event);
bool ump_enqueue_word_nonblock(struct ump_queue *q, uintptr_t val);
bool ump_enqueue_nonblock(struct ump_queue *q, 
                          uintptr_t val1,
                          uintptr_t val2,
                          uintptr_t val3,
                          uintptr_t val4,
                          uintptr_t val5,
                          uintptr_t val6,
                          uintptr_t val7);
void ump_enqueue_word(struct ump_queue *q, uintptr_t val);
void ump_enqueue(struct ump_queue *q, 
                 uintptr_t val1,
                 uintptr_t val2,
                 uintptr_t val3,
                 uintptr_t val4,
                 uintptr_t val5,
                 uintptr_t val6,
                 uintptr_t val7);
bool ump_dequeue_word_nonblock(struct ump_queue *q, uintptr_t *ret);
bool ump_dequeue_nonblock(struct ump_queue *q, uintptr_t *ret);

void ump_dequeue_word(struct ump_queue *q, uintptr_t *ret);
void ump_dequeue(struct ump_queue *q, uintptr_t *ret);
//
bool ump_can_dequeue(struct ump_queue *q);
#endif // __UMP_QUEUE_H
