#ifndef FF_QUEUE_H
#define FF_QUEUE_H

#include <inttypes.h>
#include <stddef.h>

#define CACHELINE_SIZE 64

struct __attribute__((aligned(64))) ffq_data {
    uintptr_t data[8];
};

struct ffq {
    size_t    q_size;
    struct ffq_data queue[];
};

struct ffq_src {
    size_t      head;
    struct ffq *q;
};

struct ffq_dst {
    size_t      tail;
    struct ffq *q;
};

static inline size_t
ffq_size(size_t q_size)
{
    return sizeof(struct ffq) + (q_size*CACHELINE_SIZE);
}

#define FF_EMPTY ((uintptr_t)-1) // NULL in paper

static inline size_t
ffq_next(struct ffq *ffq, size_t idx)
{
    size_t next = (idx + 1);
    if (next >= ffq->q_size) {
        next = 0;
    }
    return next;
}

void ffq_init(struct ffq *ffq, size_t q_size);
void ffq_src_init(struct ffq_src *ffq_src, struct ffq *ffq);
void ffq_dst_init(struct ffq_dst *ffq_dst, struct ffq *ffq);

void ffq_enqueue(struct ffq_src *ffq_src, uintptr_t val);
void ffq_dequeue(struct ffq_dst *ffq_dst, uintptr_t *val);

static inline bool
ffq_dequeue_nonblock(struct ffq_dst *ffq_dst, uintptr_t *val_ptr)
{
    volatile struct ffq_data *queue = ffq_dst->q->queue;
    size_t tail      = ffq_dst->tail;
    uintptr_t val    = queue[tail].data[0];

    if (val == FF_EMPTY)
        return false;

    *val_ptr = val;
    queue[tail].data[0]   = FF_EMPTY;
    ffq_dst->tail = ffq_next(ffq_dst->q, tail);
    return true;
}



bool ffq_can_dequeue(struct ffq_dst *ffq_dst);
void ffq_enqueue_full(struct ffq_src *ffq_src, 
                 uintptr_t val1,
                 uintptr_t val2,
                 uintptr_t val3,
                 uintptr_t val4,
                 uintptr_t val5,
                 uintptr_t val6,
                 uintptr_t val7);
void ffq_dequeue_full(struct ffq_dst *ffq_dst,
                 uintptr_t* buf);

#endif /* FF_QUEUE_H */
