#ifndef PT_QUEUE_H
#define PT_QUEUE_H

#include <pthread.h>
#include <inttypes.h>

// NOTE: queue size is in words (uintptr_t)

struct ptq {
    pthread_mutex_t mutex;
    pthread_cond_t  cond_full, cond_empty;
    size_t          rd, wr, count;
    size_t          q_size;
    uintptr_t       queue[];
};

static inline size_t
ptq_size(size_t q_size)
{
    return sizeof(struct ptq) + (q_size*sizeof(uintptr_t));
}

void ptq_init(struct ptq *ptq, size_t q_size);
void ptq_enqueue(struct ptq *ptq, uintptr_t val);
void ptq_dequeue(struct ptq *ptq, uintptr_t *val);

#endif
