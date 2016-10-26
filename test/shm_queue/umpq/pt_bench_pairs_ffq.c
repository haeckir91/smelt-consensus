#include <stdio.h>
#include <pthread.h>

#include "ff_queue.h"
#include "ffq_conf.h"

#include "parse_int.h"
#include "lxaffnuma.h"
#include "tsc.h"

struct bench_conf {
    int *src, *dst;
    unsigned src_size, dst_size;
};

// bidirectional (i.e., with two channels) ffq pair
// fwr: forward src->dst
// rev: reverse dst->src
struct ffq_bi_pair_state {
    struct ffq_pair_state *fwr, *rev;
};

struct thr_arg {
    unsigned              core_id;
    struct ffq_src        *send;
    struct ffq_dst        *recv;
    pthread_barrier_t     *tbar;
    unsigned              count;

    tsc_t timer;
};

static void *
src_thr(void *arg)
{
    struct thr_arg *targ = arg;

    aff_set_oncpu(targ->core_id);
    tsc_init(&targ->timer);

    // first barrier is for initialization
    pthread_barrier_wait(targ->tbar);

    uintptr_t val=0;

    for (unsigned i=0; i<targ->count; i++) {

        // send
        tsc_start(&targ->timer);
        ffq_enqueue(targ->send, val);
        tsc_pause(&targ->timer);

        // receive
        uintptr_t rval;
        ffq_dequeue(targ->recv, &rval);
        assert(val == rval);

        val++;
    }

    // second barrier is for the main thread to measure time
    pthread_barrier_wait(targ->tbar);

    return NULL;
}

static void *
dst_thr(void *arg)
{
    struct thr_arg *targ = arg;

    aff_set_oncpu(targ->core_id);
    tsc_init(&targ->timer);

    // first barrier is for initialization
    pthread_barrier_wait(targ->tbar);

    uintptr_t val=0;
    for (unsigned i=0; i<targ->count; i++) {

        // receive
        uintptr_t rval;
        ffq_dequeue(targ->recv, &rval);
        assert(val == rval);

        //send
        //tsc_start(&targ->timer);
        ffq_enqueue(targ->send, val);
        //tsc_pause(&targ->timer);

        val++;
    }

    // second barrier is for the main thread to measure time
    pthread_barrier_wait(targ->tbar);

    return NULL;
}

#define FFQ_CONF_INIT(src_, dst_, shm_size_)      \
{                                                 \
    .src = {                                      \
         .core_id = src_,                         \
    },                                            \
    .dst = {                                      \
         .core_id = dst_,                         \
    },                                            \
    .shm_numa_node = numa_cpu_to_node(src_),      \
    .qsize = shm_size_                            \
}

static void
do_pair_bench(int src, int dst)
{
    const int shm_size = 8192;
    const unsigned count = 1000000;

    struct ffq_bi_pair_state st;

    struct ffq_pair_conf fwr_conf = FFQ_CONF_INIT(src, dst, shm_size);
    struct ffq_pair_conf rev_conf = FFQ_CONF_INIT(dst, src, shm_size);

    st.fwr = ffq_pair_state_create(&fwr_conf);
    st.rev = ffq_pair_state_create(&rev_conf);

    pthread_t tids[2];
    struct thr_arg targs[2];
    pthread_barrier_t tbar;

    pthread_barrier_init(&tbar, NULL, 3);

    // forward direction
    targs[0].core_id = src;
    targs[0].send = &st.fwr->src;
    targs[0].recv = &st.rev->dst;
    targs[0].tbar = &tbar;
    targs[0].count = count;

    // reverse direction
    targs[1].core_id = dst;
    targs[1].send = &st.rev->src;
    targs[1].recv = &st.fwr->dst;
    targs[1].tbar = &tbar;
    targs[1].count = count;

    pthread_create(&tids[0], NULL, src_thr, &targs[0]);
    pthread_create(&tids[1], NULL, dst_thr, &targs[1]);

    tsc_t tsc; tsc_init(&tsc);
    // all threads initialize themselves and execute a barrier when ready
    pthread_barrier_wait(&tbar);
    tsc_start(&tsc);
    // all threads call a barrier before they finish
    pthread_barrier_wait(&tbar);
    tsc_pause(&tsc);

    pthread_join(tids[0], NULL);
    pthread_join(tids[1], NULL);

	tsc_report("total", &tsc);
	tsc_report("sending", &targs[0].timer);
}

int main(int argc, const char *argv[])
{
    struct bench_conf bconf;

    if (argc < 3) {
        printf("Usage: %s <src> <dst>\n", argv[0]);
        printf(" src/dst: are core ranges (e.g., 1-10,20)\n");
        printf(" Example: %s 1-4 1-4\n", argv[0]);
        printf(" i->i is ignored\n");
        exit(0);
    }

    bconf.src = parse_ints_range(argv[1], &bconf.src_size);
    bconf.dst = parse_ints_range(argv[2], &bconf.dst_size);

    int src, dst;
    for (unsigned si=0; si<bconf.src_size; si++)
        for (unsigned di=0; di<bconf.dst_size; di++)
            if ( (src = bconf.src[si]) != (dst = bconf.dst[di])) {
                printf("%d -> %d\n", src, dst);
                do_pair_bench(src, dst);
            }

    return 0;
}
