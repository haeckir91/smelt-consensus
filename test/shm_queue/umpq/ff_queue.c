#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include "ff_queue.h"

void
ffq_init(struct ffq *ffq, size_t q_size)
{
    ffq->q_size = q_size;
    for (unsigned i=0; i<q_size; i++)
        ffq->queue[i].data[0] = FF_EMPTY;
}

void
ffq_src_init(struct ffq_src *ffq_src, struct ffq *ffq)
{
    ffq_src->q    = ffq;
    ffq_src->head = 0;
}

void
ffq_dst_init(struct ffq_dst *ffq_dst, struct ffq *ffq)
{
    ffq_dst->q    = ffq;
    ffq_dst->tail = 0;
}

static inline bool
ffq_enqueue_nonblock(struct ffq_src *ffq_src, uintptr_t val)
{
    volatile struct ffq_data *queue = ffq_src->q->queue;
    size_t head      = ffq_src->head;

    if (queue[head].data[0] != FF_EMPTY)
        return false;

    assert(val != FF_EMPTY);
    queue[head].data[0]   = val;
    ffq_src->head = ffq_next(ffq_src->q, head);
    return true;
}

static inline bool
ffq_enqueue_full_nonblock(struct ffq_src *ffq_src, 
                     uintptr_t val1,
                     uintptr_t val2,
                     uintptr_t val3,
                     uintptr_t val4,
                     uintptr_t val5,
                     uintptr_t val6,
                     uintptr_t val7)
{
    volatile struct ffq_data *queue = ffq_src->q->queue;
    size_t head      = ffq_src->head;

    if (queue[head].data[0] != FF_EMPTY)
        return false;

    assert(val1 != FF_EMPTY);

    queue[head].data[1]   = val2;
    queue[head].data[2]   = val3;
    queue[head].data[3]   = val4;
    queue[head].data[4]   = val5;
    queue[head].data[5]   = val6;
    queue[head].data[6]   = val7;

    // Needs to be written last
    queue[head].data[0]   = val1;

    ffq_src->head = ffq_next(ffq_src->q, head);
    return true;
}

bool ffq_can_dequeue(struct ffq_dst *ffq_dst)
{
    volatile struct ffq_data *queue = ffq_dst->q->queue;
    size_t tail      = ffq_dst->tail;
    uintptr_t val    = queue[tail].data[0];
    if (val == FF_EMPTY) {
        return false;
    } else {
        return true;
    }   
}
static inline bool
ffq_dequeue_full_nonblock(struct ffq_dst *ffq_dst, 
                     uintptr_t *buf)
{
    volatile struct ffq_data *queue = ffq_dst->q->queue;
    size_t tail      = ffq_dst->tail;
    uintptr_t val    = queue[tail].data[0];

    if (val == FF_EMPTY)
        return false;

    buf[0] = queue[tail].data[0];
    buf[1] = queue[tail].data[1];
    buf[2] = queue[tail].data[2];
    buf[3] = queue[tail].data[3];
    buf[4] = queue[tail].data[4];
    buf[5] = queue[tail].data[5];
    buf[6] = queue[tail].data[6];

    queue[tail].data[0]   = FF_EMPTY;
    ffq_dst->tail = ffq_next(ffq_dst->q, tail);
    return true;
}


/* hint the processor that this is a spin-loop */
static inline void
relax_cpu(void)
{
	__asm__ __volatile__("rep; nop" ::: "memory");
}


// TODO: blocking
void
ffq_enqueue(struct ffq_src *ffq_src, uintptr_t val)
{
    bool ok;
    do {
        ok = ffq_enqueue_nonblock(ffq_src, val);
    } while (!ok);
}

// TODO: blocking
void
ffq_dequeue(struct ffq_dst *ffq_dst, uintptr_t *val)
{
    bool ok;
    do {
        ok = ffq_dequeue_nonblock(ffq_dst, val);
    } while (!ok);
}

// TODO: blocking
void
ffq_enqueue_full(struct ffq_src *ffq_src, 
                 uintptr_t val1,
                 uintptr_t val2,
                 uintptr_t val3,
                 uintptr_t val4,
                 uintptr_t val5,
                 uintptr_t val6,
                 uintptr_t val7)
{
    bool ok;
    do {
        ok = ffq_enqueue_full_nonblock(ffq_src, val1,
                                  val2, val3, val4, val5,
                                  val6, val7);
    } while (!ok);
}

// TODO: blocking
void
ffq_dequeue_full(struct ffq_dst *ffq_dst, 
                 uintptr_t *buf)
{
    bool ok;
    do {
        ok = ffq_dequeue_full_nonblock(ffq_dst, buf);
    } while (!ok);
}

