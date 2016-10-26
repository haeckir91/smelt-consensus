#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <assert.h>

#include "ff_queue.h"
#include "ffq_conf.h"

#include "misc.h"
#include "parse_int.h"
#include "lxaffnuma.h"
#include "bfcompat.h" // STATIC_ASSERT()

#define CL_SIZE 64 // cache-line size
#if defined(__GNUC__)
#define CACHE_ALIGNED __attribute__ ((aligned(CL_SIZE)))
#else
#define CACHE_ALIGNED
#warning "Don't know how to align struct"
#endif

// test consists of a set of ffq pairs kept here
struct ffq_bench_conf {
    struct ffq_pair_conf *head;
    unsigned pairs_nr;
};

/*************************************************************************
 *
 * Parse arguments and generate configuration
 *
 ***********************************************/

static void
print_conf(struct ffq_bench_conf *conf)
{
    printf("------- %u pair(s) ------\n", conf->pairs_nr);
    struct ffq_pair_conf *pair = conf->head;
    for (unsigned i=0; i<conf->pairs_nr; i++) {
        if (i != 0) printf("\n");
        ffq_pair_conf_print(pair);
        pair = pair->next;
    }
    assert(pair == NULL);
}


static struct ffq_bench_conf *
parse_options(int argc, const char *argv[])
{
    struct ffq_bench_conf *ret;
    ret = xmalloc(sizeof(*ret));

    ret->head = NULL;
    ret->pairs_nr = 0;
    struct ffq_pair_conf **next_ptr = &ret->head;

    for (unsigned i=1; i<argc; i++) {
        *next_ptr = ffq_pair_conf_parse(argv[i]);
        ret->pairs_nr++;
        next_ptr = &((*next_ptr)->next);
    }

    return ret;
}


/*************************************************************************
 *
 * Pthreads and main()
 *
 *****************************/

#include <pthread.h>
#include "tsc.h"

#define COUNT 100000000 // message count for each thread

enum {
    THR_BENCH_SRC,
    THR_BENCH_DST
};

struct thr_arg {
    struct ffq_peer_conf  *conf;
    pthread_barrier_t     *tbar;

    union {
        struct ffq_src *src;
        struct ffq_dst *dst;
    }                      ffq;
    int                    type;

    tsc_t                  timer;
};

static void
bench_src_thread(struct ffq_src *q, size_t cycles_delay)
{
    uintptr_t val = 0;
    for (size_t i=0; i<COUNT; i++) {
        ffq_enqueue(q, val++);
        if (cycles_delay)
            tsc_spinticks(cycles_delay);
    }
}

static void
bench_dst_thread(struct ffq_dst *q, size_t cycles_delay)
{
    uintptr_t val;
    for (size_t i=0; i<COUNT; i++) {
        ffq_dequeue(q, &val);
        assert(val == i);
        if (cycles_delay)
            tsc_spinticks(cycles_delay);
    }
}

void *
thread_fn(void *arg)
{
    struct thr_arg *targ = (struct thr_arg *)arg;
    aff_set_oncpu(targ->conf->core_id);
    tsc_init(&targ->timer);

    // first barrier is for initialization
    pthread_barrier_wait(targ->tbar);

    tsc_start(&targ->timer);
    switch (targ->type) {
        case THR_BENCH_SRC:
        bench_src_thread(targ->ffq.src, targ->conf->delay);
        break;

        case THR_BENCH_DST:
        bench_dst_thread(targ->ffq.dst, targ->conf->delay);
        break;

        default:
        printf("Unknown thread type:%d\n", targ->type);
        abort();
    }
    tsc_pause(&targ->timer);

    // second barrier is for the main thread to measure time
    pthread_barrier_wait(targ->tbar);

    return NULL;
}

int main(int argc, const char *argv[])
{

    // create a configuration based on arguments
    struct ffq_bench_conf *conf;
    conf = parse_options(argc, argv);

    if (conf->head == NULL) {
        printf("Usage: %s [pairs]\n", argv[0]);
        ffq_pair_conf_help();
        exit(0);
    }
    print_conf(conf);

    // initialize FF state (i.e., chans/queues) based on the configuration
    struct ffq_pair_state *pairs_state[conf->pairs_nr];
    struct ffq_pair_conf  *pair_conf = conf->head;
    for (unsigned int i=0; i<conf->pairs_nr; i++) {
        pairs_state[i] = ffq_pair_state_create(pair_conf);
        pair_conf = pair_conf->next;
    }

    // initialize and start threads for each pair
    int               nthreads = conf->pairs_nr<<1;
    pthread_t         tids[nthreads];
    struct thr_arg    targs[nthreads];
    pthread_barrier_t tbar;

    pthread_barrier_init(&tbar, NULL, nthreads+1);

    pair_conf = conf->head;
    for (unsigned int i=0; i<conf->pairs_nr; i++) {
        struct ffq_pair_state *st = pairs_state[i];
        struct thr_arg *src_targ = targs + (i<<1);
        struct thr_arg *dst_targ = src_targ++;

        src_targ->conf    = &pair_conf->src;
        src_targ->ffq.src = &st->src;
        src_targ->type    = THR_BENCH_SRC;
        src_targ->tbar    = &tbar;

        dst_targ->conf    = &pair_conf->dst;
        dst_targ->ffq.dst = &st->dst;
        dst_targ->type    = THR_BENCH_DST;
        dst_targ->tbar    = &tbar;

        pair_conf = pair_conf->next;
    }
    assert(pair_conf == NULL);

    for (unsigned int i=0; i<nthreads; i++) {
        pthread_create(tids + i, NULL, thread_fn, targs + i);
    }

    tsc_t tsc; tsc_init(&tsc);
    // all threads initialize themselves and execute a barrier when ready
    pthread_barrier_wait(&tbar);
    tsc_start(&tsc);
    // all threads call a barrier before they finish
    pthread_barrier_wait(&tbar);
    tsc_pause(&tsc);

    // join threads
    for (unsigned int i=0; i<nthreads; i++) {
        pthread_join(tids[i], NULL);
    }

    uint64_t ticks = tsc_getticks(&tsc);
	printf("total:%s ticks [%s ticks/msg]\n", ul_hstr(ticks), ul_hstr(ticks/COUNT));
}
