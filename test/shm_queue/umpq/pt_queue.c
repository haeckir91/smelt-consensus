#include <pthread.h>
#include <inttypes.h>

#include "pt_queue.h"

// NOTE: queue size is in words (uintptr_t)

void
ptq_init(struct ptq *ptq, size_t q_size)
{
    ptq->q_size = q_size;
    ptq->count = ptq->wr = ptq->rd = 0;

    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    //pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_ADAPTIVE_NP);
    pthread_mutex_init(&ptq->mutex, &mutex_attr);
    pthread_cond_init(&ptq->cond_full, NULL);
    pthread_cond_init(&ptq->cond_empty, NULL);
}

void
ptq_enqueue(struct ptq *ptq, uintptr_t val)
{
    pthread_mutex_lock(&ptq->mutex);
    while (ptq->count == ptq->q_size)
        pthread_cond_wait(&ptq->cond_full, &ptq->mutex);

    ptq->queue[ptq->wr] = val;
    ptq->wr = (ptq->wr + 1) % ptq->q_size;
    ptq->count++;

    if (ptq->count == 1)
        pthread_cond_signal(&ptq->cond_empty);
    pthread_mutex_unlock(&ptq->mutex);
}

void
ptq_dequeue(struct ptq *ptq, uintptr_t *val)
{
    pthread_mutex_lock(&ptq->mutex);
    while (ptq->count == 0)
        pthread_cond_wait(&ptq->cond_empty, &ptq->mutex);

    *val = ptq->queue[ptq->rd];
    ptq->rd = (ptq->rd + 1) % ptq->q_size;
    ptq->count--;

    if (ptq->count == ptq->q_size - 1)
        pthread_cond_signal(&ptq->cond_full);
    pthread_mutex_unlock(&ptq->mutex);
}
